# Memory trace

## Usage

```bash
# Step 1. Create memory journal code
make
./build/rv32emu ./build/hello.elf 2> journal.txt

# Step 2. Run journal.txt through analyzer
g++ -Wall -Wextra -Wshadow --std=c++20 -O2 tools/mem_trc/parse_log.cc -o parse_log
./parse_log journey.txt
```

## Result

```
Parse complete

== Replay Results ==
Number of Mallocs:    24
Number of Frees:      24
Median Allocation:    72 bytes
Average Malloc Time:  375 nanoseconds

Alloc Time
Best:    0.00 microseconds
p1:      0.00 microseconds
p10:     0.00 microseconds
p25:     0.00 microseconds
p50:     0.00 microseconds
p75:     1.00 microseconds
p90:     1.00 microseconds
p95:     1.00 microseconds
p98:     2.00 microseconds
p99:     2.00 microseconds
p99.9:   2.00 microseconds
p99.99:  2.00 microseconds
p99.999: 2.00 microseconds
Worst:   2.00 microseconds

Free Time
Best:    0.00 microseconds
p1:      0.00 microseconds
p10:     0.00 microseconds
p25:     0.00 microseconds
p50:     1.00 microseconds
p75:     1.00 microseconds
p90:     1.00 microseconds
p95:     1.00 microseconds
p98:     2.00 microseconds
p99:     2.00 microseconds
p99.9:   2.00 microseconds
p99.99:  2.00 microseconds
p99.999: 2.00 microseconds
Worst:   2.00 microseconds
```