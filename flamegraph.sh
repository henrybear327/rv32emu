#!/bin/bash

make clean
make ENABLE_JIT=0
sudo perf record -F 999 -g -- ./build/rv32emu ./build/coremark.elf 
sudo perf script > out.perf
../FlameGraph/stackcollapse-perf.pl out.perf > out.folded 
../FlameGraph/flamegraph.pl out.folded > coremark-no-JIT.svg

make clean
make ENABLE_JIT=1
sudo perf record -F 999 -g -- ./build/rv32emu ./build/coremark.elf 
sudo perf script > out.perf
../FlameGraph/stackcollapse-perf.pl out.perf > out.folded 
../FlameGraph/flamegraph.pl out.folded > coremark-with-JIT.svg

make clean
make ENABLE_JIT=0
sudo perf record -F 999 -g -- ./build/rv32emu ./build/dhrystone.elf 
sudo perf script > out.perf
../FlameGraph/stackcollapse-perf.pl out.perf > out.folded 
../FlameGraph/flamegraph.pl out.folded > dhrystone-no-JIT.svg

make clean
make ENABLE_JIT=1
sudo perf record -F 999 -g -- ./build/rv32emu ./build/dhrystone.elf 
sudo perf script > out.perf
../FlameGraph/stackcollapse-perf.pl out.perf > out.folded 
../FlameGraph/flamegraph.pl out.folded > dhrystone-with-JIT.svg
