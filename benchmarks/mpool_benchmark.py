#!/usr/bin/env python3
"""
Memory pool benchmark runner for rv32emu.

Compares default mpool vs TLSF implementations by running
ELF binaries and collecting performance metrics.

Usage:
    python3 benchmarks/mpool_benchmark.py --runs 5
    python3 benchmarks/mpool_benchmark.py --binaries dhrystone coremark
    python3 benchmarks/mpool_benchmark.py --json results.json --quiet
"""

import argparse
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

# Configuration
DEFAULT_RUNS = 5
DEFAULT_TIMEOUT = 300  # seconds


# ============================================================================
# Data Classes
# ============================================================================

@dataclass
class MpoolStats:
    """Statistics for a single memory pool."""
    pool_name: str
    alloc_count: int
    alloc_time_ms: float
    free_count: int
    free_time_ms: float
    extend_count: int
    extend_time_ms: float


@dataclass
class BinaryRun:
    """Result from a single benchmark run."""
    binary_name: str
    config: str  # "default" or "tlsf"
    exit_code: int
    pools: Dict[str, MpoolStats]
    stdout: str
    stderr: str
    execution_time_s: float

    def is_valid(self) -> bool:
        """Check if run completed successfully and has stats."""
        return self.exit_code == 0 and len(self.pools) > 0


@dataclass
class AggregatedStats:
    """Aggregated statistics across multiple runs."""
    mean: float
    stdev: float
    p50: float
    p90: float
    p95: float
    p99: float
    min_val: float
    max_val: float
    samples: int


@dataclass
class PoolComparison:
    """Comparison between default and TLSF for one pool metric."""
    pool_name: str
    metric_name: str
    default: AggregatedStats
    tlsf: AggregatedStats

    @property
    def delta_percent(self) -> float:
        """Calculate percentage difference (TLSF vs default)."""
        if self.default.mean == 0:
            return 0.0
        return ((self.tlsf.mean - self.default.mean) / self.default.mean) * 100


@dataclass
class BinaryComparison:
    """Full comparison for one binary."""
    binary_name: str
    runs_completed: int
    pools: Dict[str, List[PoolComparison]] = field(default_factory=dict)


# ============================================================================
# Core Classes
# ============================================================================

class StatsParser:
    """Parses mpool statistics from emulator stderr."""

    # Regex pattern matching the exact format from riscv.c
    MPOOL_PATTERN = re.compile(
        r'mpool\[(?P<pool_name>\w+)\]:\s+'
        r'alloc=(?P<alloc_count>\d+)\((?P<alloc_time>[\d.]+)ms\)\s+'
        r'free=(?P<free_count>\d+)\((?P<free_time>[\d.]+)ms\)\s+'
        r'extend=(?P<extend_count>\d+)\((?P<extend_time>[\d.]+)ms\)'
    )

    @classmethod
    def parse_stderr(cls, stderr: str) -> Dict[str, MpoolStats]:
        """Parse all mpool stats from stderr output."""
        pools = {}
        for match in cls.MPOOL_PATTERN.finditer(stderr):
            stats = MpoolStats(
                pool_name=match.group('pool_name'),
                alloc_count=int(match.group('alloc_count')),
                alloc_time_ms=float(match.group('alloc_time')),
                free_count=int(match.group('free_count')),
                free_time_ms=float(match.group('free_time')),
                extend_count=int(match.group('extend_count')),
                extend_time_ms=float(match.group('extend_time'))
            )
            pools[stats.pool_name] = stats
        return pools

    @classmethod
    def validate_output(cls, stderr: str) -> bool:
        """Check if stderr contains mpool statistics."""
        return cls.MPOOL_PATTERN.search(stderr) is not None


class BuildError(Exception):
    """Raised when emulator build fails."""
    pass


class EmulatorBuilder:
    """Handles building emulator with different configurations."""

    def __init__(self, no_build: bool = False, quiet: bool = False):
        self.no_build = no_build
        self.quiet = quiet
        self.build_dir = Path("build")

    def build_default(self) -> Path:
        """Build emulator with default mpool."""
        if self.no_build:
            return self._verify_binary("build/rv32emu")

        output_path = self.build_dir / "rv32emu_default"
        self._run_build(config_tlsf=False, output_path=output_path)
        return output_path

    def build_tlsf(self) -> Path:
        """Build emulator with TLSF mpool."""
        if self.no_build:
            return self._verify_binary("build/rv32emu")

        output_path = self.build_dir / "rv32emu_tlsf"
        self._run_build(config_tlsf=True, output_path=output_path)
        return output_path

    def _run_build(self, config_tlsf: bool, output_path: Path) -> None:
        """Execute build commands."""
        try:
            if not self.quiet:
                config_name = "TLSF" if config_tlsf else "default"
                print(f"Building {config_name} emulator...")

            # Clean previous build
            self._run_command(["make", "clean"])

            # Configure
            self._run_command(["make", "defconfig"])

            # Build with optional CONFIG_TLSF
            build_cmd = ["make"]
            if config_tlsf:
                build_cmd.append("CONFIG_TLSF=1")

            self._run_command(build_cmd)

            # Copy binary to config-specific location
            shutil.copy("build/rv32emu", output_path)

            if not self.quiet:
                print(f"  → {output_path}")

        except subprocess.CalledProcessError as e:
            config_name = "TLSF" if config_tlsf else "default"
            raise BuildError(
                f"Failed to build {config_name} emulator\n"
                f"Command: {' '.join(e.cmd)}\n"
                f"Exit code: {e.returncode}\n"
                f"Output: {e.stdout}\n{e.stderr}"
            )

    def _run_command(self, cmd: List[str]) -> subprocess.CompletedProcess:
        """Run shell command with error handling."""
        return subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=True,
            timeout=600  # 10 minute timeout for builds
        )

    def _verify_binary(self, path: str) -> Path:
        """Verify binary exists when --no-build is used."""
        binary = Path(path)
        if not binary.exists():
            raise FileNotFoundError(
                f"Emulator binary not found: {path}\n"
                "Run without --no-build or build manually first."
            )
        return binary


class BenchmarkRunner:
    """Executes binaries and collects mpool statistics."""

    def __init__(self, timeout: int = DEFAULT_TIMEOUT, quiet: bool = False):
        self.timeout = timeout
        self.quiet = quiet

    def run_binary(
        self,
        emulator_path: Path,
        binary_path: Path,
        config: str
    ) -> BinaryRun:
        """Run a single binary and collect stats."""
        start_time = time.monotonic()

        try:
            result = subprocess.run(
                [str(emulator_path), str(binary_path)],
                capture_output=True,
                text=True,
                timeout=self.timeout
            )

            execution_time = time.monotonic() - start_time

            # Parse mpool stats from stderr
            pools = StatsParser.parse_stderr(result.stderr)

            return BinaryRun(
                binary_name=binary_path.name,
                config=config,
                exit_code=result.returncode,
                pools=pools,
                stdout=result.stdout,
                stderr=result.stderr,
                execution_time_s=execution_time
            )

        except subprocess.TimeoutExpired as e:
            execution_time = time.monotonic() - start_time
            return BinaryRun(
                binary_name=binary_path.name,
                config=config,
                exit_code=-1,
                pools={},
                stdout=e.stdout.decode() if e.stdout else "",
                stderr=f"TIMEOUT after {self.timeout}s",
                execution_time_s=execution_time
            )
        except Exception as e:
            execution_time = time.monotonic() - start_time
            return BinaryRun(
                binary_name=binary_path.name,
                config=config,
                exit_code=-1,
                pools={},
                stdout="",
                stderr=f"ERROR: {str(e)}",
                execution_time_s=execution_time
            )

    def run_benchmark_set(
        self,
        emulator_path: Path,
        binary_path: Path,
        config: str,
        n_runs: int
    ) -> List[BinaryRun]:
        """Run binary multiple times."""
        runs = []
        for i in range(n_runs):
            if not self.quiet:
                print(f"  Run {i+1}/{n_runs} ({config})...", end="\r", flush=True)

            run = self.run_binary(emulator_path, binary_path, config)
            runs.append(run)

            # Fail fast if binary doesn't work
            if i == 0 and not run.is_valid():
                if not self.quiet:
                    print(f"\n  Warning: {binary_path.name} failed on first run ({config})")
                    if run.stderr:
                        print(f"  Error: {run.stderr[:200]}")
                break

        if not self.quiet and runs and runs[0].is_valid():
            print()  # Clear the progress line

        return runs


class StatisticsCalculator:
    """Computes aggregated statistics from multiple runs."""

    @staticmethod
    def calculate(values: List[float]) -> AggregatedStats:
        """Calculate mean, stdev, and percentiles."""
        if not values:
            return AggregatedStats(0, 0, 0, 0, 0, 0, 0, 0, 0)

        values_sorted = sorted(values)
        n = len(values)

        return AggregatedStats(
            mean=statistics.mean(values),
            stdev=statistics.stdev(values) if n > 1 else 0.0,
            p50=statistics.median(values),
            p90=values_sorted[int(n * 0.90)] if n > 1 else values[0],
            p95=values_sorted[int(n * 0.95)] if n > 1 else values[0],
            p99=values_sorted[int(n * 0.99)] if n > 1 else values[0],
            min_val=min(values),
            max_val=max(values),
            samples=n
        )

    @staticmethod
    def aggregate_runs(
        runs: List[BinaryRun]
    ) -> Dict[str, Dict[str, AggregatedStats]]:
        """
        Aggregate statistics across runs.
        Returns: {pool_name: {metric_name: AggregatedStats}}
        """
        # Filter valid runs
        valid_runs = [r for r in runs if r.is_valid()]
        if not valid_runs:
            return {}

        # Group by pool name
        pool_metrics: Dict[str, Dict[str, List[float]]] = {}

        for run in valid_runs:
            for pool_name, stats in run.pools.items():
                if pool_name not in pool_metrics:
                    pool_metrics[pool_name] = {
                        'alloc_count': [],
                        'alloc_time': [],
                        'free_count': [],
                        'free_time': [],
                        'extend_count': [],
                        'extend_time': []
                    }

                pool_metrics[pool_name]['alloc_count'].append(float(stats.alloc_count))
                pool_metrics[pool_name]['alloc_time'].append(stats.alloc_time_ms)
                pool_metrics[pool_name]['free_count'].append(float(stats.free_count))
                pool_metrics[pool_name]['free_time'].append(stats.free_time_ms)
                pool_metrics[pool_name]['extend_count'].append(float(stats.extend_count))
                pool_metrics[pool_name]['extend_time'].append(stats.extend_time_ms)

        # Calculate statistics for each metric
        result = {}
        for pool_name, metrics in pool_metrics.items():
            result[pool_name] = {
                metric_name: StatisticsCalculator.calculate(values)
                for metric_name, values in metrics.items()
            }

        return result


class ResultFormatter:
    """Formats benchmark results for display and JSON output."""

    @staticmethod
    def print_table(comparisons: List[BinaryComparison]) -> None:
        """Print side-by-side comparison table."""
        print("=" * 88)
        print("Memory Pool Benchmark Results")
        print("=" * 88)

        for comparison in comparisons:
            print(f"\nBinary: {comparison.binary_name} ({comparison.runs_completed} runs)")
            print("-" * 88)

            # Header
            print(f"{'Pool':<12} {'Metric':<13} | {'Default mpool':<22} | {'TLSF mpool':<22} | {'Delta':<8}")
            print("-" * 88)

            for pool_name, pool_comparisons in sorted(comparison.pools.items()):
                for i, comp in enumerate(pool_comparisons):
                    pool_label = pool_name if i == 0 else ""

                    # Format metric value with appropriate unit
                    def format_metric(stats: AggregatedStats, metric: str) -> str:
                        if "count" in metric:
                            return f"{stats.mean:.0f} ops"
                        else:  # time metrics
                            return f"{stats.mean:.3f} ± {stats.stdev:.3f} ms"

                    default_str = format_metric(comp.default, comp.metric_name)
                    tlsf_str = format_metric(comp.tlsf, comp.metric_name)

                    # Calculate and format delta
                    delta = comp.delta_percent
                    if abs(delta) < 0.1:
                        delta_str = "~0%"
                    else:
                        delta_str = f"{delta:+.1f}%"

                    print(f"{pool_label:<12} {comp.metric_name:<13} | "
                          f"{default_str:<22} | {tlsf_str:<22} | {delta_str:<8}")

        print()

    @staticmethod
    def export_json(comparisons: List[BinaryComparison], output_path: Path) -> None:
        """Export results to JSON for CI integration."""
        data = []

        for comparison in comparisons:
            binary_data = {
                "binary": comparison.binary_name,
                "runs": comparison.runs_completed,
                "pools": {}
            }

            for pool_name, pool_comparisons in comparison.pools.items():
                pool_data = []
                for comp in pool_comparisons:
                    pool_data.append({
                        "metric": comp.metric_name,
                        "default": {
                            "mean": comp.default.mean,
                            "stdev": comp.default.stdev,
                            "p50": comp.default.p50,
                            "p90": comp.default.p90,
                            "p95": comp.default.p95,
                            "p99": comp.default.p99
                        },
                        "tlsf": {
                            "mean": comp.tlsf.mean,
                            "stdev": comp.tlsf.stdev,
                            "p50": comp.tlsf.p50,
                            "p90": comp.tlsf.p90,
                            "p95": comp.tlsf.p95,
                            "p99": comp.tlsf.p99
                        },
                        "delta_percent": comp.delta_percent
                    })
                binary_data["pools"][pool_name] = pool_data

            data.append(binary_data)

        with open(output_path, 'w') as f:
            json.dump(data, f, indent=2)


# ============================================================================
# Utility Functions
# ============================================================================

def discover_binaries(binary_dir: Path = Path("build/riscv32")) -> List[Path]:
    """Auto-discover ELF binaries in build directory."""
    if not binary_dir.exists():
        # Try to download artifacts
        print(f"Binary directory not found: {binary_dir}")
        print("Downloading test artifacts...")
        try:
            subprocess.run(
                ["make", "artifact"],
                capture_output=True,
                text=True,
                check=True,
                timeout=600
            )
        except subprocess.CalledProcessError as e:
            raise RuntimeError(
                f"Failed to download artifacts\n"
                f"Exit code: {e.returncode}\n"
                f"Output: {e.stdout}\n{e.stderr}"
            )

    # Find all executable files
    binaries = []
    for file in binary_dir.iterdir():
        if file.is_file() and not file.suffix and os.access(file, os.X_OK):
            # Verify it's an ELF file
            try:
                result = subprocess.run(
                    ["file", str(file)],
                    capture_output=True,
                    text=True,
                    timeout=5
                )
                if "ELF" in result.stdout:
                    binaries.append(file)
            except:
                continue  # Skip files that can't be checked

    return sorted(binaries)


def build_comparison(
    binary_name: str,
    runs_completed: int,
    default_stats: Dict[str, Dict[str, AggregatedStats]],
    tlsf_stats: Dict[str, Dict[str, AggregatedStats]]
) -> BinaryComparison:
    """Build comparison object from aggregated statistics."""
    pools = {}

    # Get union of pool names
    all_pools = set(default_stats.keys()) | set(tlsf_stats.keys())

    for pool_name in sorted(all_pools):
        if pool_name not in default_stats or pool_name not in tlsf_stats:
            continue  # Skip pools not present in both configs

        comparisons = []

        # Compare each metric
        metrics = ['alloc_count', 'alloc_time', 'free_count', 'free_time', 'extend_count', 'extend_time']
        for metric in metrics:
            if metric in default_stats[pool_name] and metric in tlsf_stats[pool_name]:
                comp = PoolComparison(
                    pool_name=pool_name,
                    metric_name=metric,
                    default=default_stats[pool_name][metric],
                    tlsf=tlsf_stats[pool_name][metric]
                )
                comparisons.append(comp)

        pools[pool_name] = comparisons

    return BinaryComparison(
        binary_name=binary_name,
        runs_completed=runs_completed,
        pools=pools
    )


# ============================================================================
# Main Workflow
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Benchmark rv32emu memory pool implementations (default vs TLSF)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s                              # Run all binaries, 5 runs each
  %(prog)s --runs 10                    # Run all binaries, 10 runs each
  %(prog)s --binaries dhrystone         # Run specific binary only
  %(prog)s --binaries dhrystone coremark --runs 3
  %(prog)s --json output.json           # Export to JSON
  %(prog)s --no-build                   # Skip rebuilding (use existing)
  %(prog)s --quiet                      # Minimal output
        """
    )

    parser.add_argument(
        "--runs",
        type=int,
        default=DEFAULT_RUNS,
        metavar="N",
        help=f"Number of iterations per binary (default: {DEFAULT_RUNS})"
    )
    parser.add_argument(
        "--binaries",
        nargs="+",
        metavar="NAME",
        help="Specific binaries to test (otherwise all in build/riscv32/)"
    )
    parser.add_argument(
        "--json",
        metavar="FILE",
        help="Export results to JSON file for CI integration"
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress verbose output"
    )
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Skip rebuilding emulator (use existing build/rv32emu)"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT,
        metavar="SECONDS",
        help=f"Timeout per binary run in seconds (default: {DEFAULT_TIMEOUT})"
    )

    args = parser.parse_args()

    # Validate runs
    if args.runs < 1:
        parser.error("--runs must be at least 1")

    try:
        # 1. Discover binaries
        if not args.quiet:
            print("Discovering test binaries...")

        if args.binaries:
            binary_dir = Path("build/riscv32")
            binaries = [binary_dir / name for name in args.binaries]
            # Validate they exist
            for binary in binaries:
                if not binary.exists():
                    raise FileNotFoundError(f"Binary not found: {binary}")
        else:
            binaries = discover_binaries()

        if not binaries:
            raise RuntimeError("No test binaries found")

        if not args.quiet:
            print(f"Found {len(binaries)} binaries: {', '.join(b.name for b in binaries)}\n")

        # 2. Build emulators
        if not args.quiet:
            print("Building emulators...")

        builder = EmulatorBuilder(no_build=args.no_build, quiet=args.quiet)
        default_emu = builder.build_default()
        tlsf_emu = builder.build_tlsf()

        if not args.quiet:
            print()

        # 3. Run benchmarks
        runner = BenchmarkRunner(timeout=args.timeout, quiet=args.quiet)
        all_results = []

        for binary in binaries:
            if not args.quiet:
                print(f"Benchmarking: {binary.name}")

            # Run with default mpool
            if not args.quiet:
                print(f"  Running with default mpool...")
            default_runs = runner.run_benchmark_set(
                default_emu, binary, "default", args.runs
            )

            # Run with TLSF mpool
            if not args.quiet:
                print(f"  Running with TLSF mpool...")
            tlsf_runs = runner.run_benchmark_set(
                tlsf_emu, binary, "tlsf", args.runs
            )

            # Aggregate statistics
            default_stats = StatisticsCalculator.aggregate_runs(default_runs)
            tlsf_stats = StatisticsCalculator.aggregate_runs(tlsf_runs)

            # Build comparison
            if default_stats and tlsf_stats:
                comparison = build_comparison(
                    binary.name,
                    min(len([r for r in default_runs if r.is_valid()]),
                        len([r for r in tlsf_runs if r.is_valid()])),
                    default_stats,
                    tlsf_stats
                )
                all_results.append(comparison)
            else:
                if not args.quiet:
                    print(f"  Warning: No valid results for {binary.name}\n")

        # 4. Output results
        if all_results:
            print()
            ResultFormatter.print_table(all_results)

            if args.json:
                ResultFormatter.export_json(all_results, Path(args.json))
                if not args.quiet:
                    print(f"Results exported to: {args.json}")
        else:
            print("\nNo valid benchmark results to display.")
            sys.exit(1)

    except KeyboardInterrupt:
        print("\n\nBenchmark interrupted by user", file=sys.stderr)
        sys.exit(130)
    except Exception as e:
        print(f"\nError: {e}", file=sys.stderr)
        if not args.quiet:
            import traceback
            traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
