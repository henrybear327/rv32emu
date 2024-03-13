#!/bin/bash

# compile log parser
g++ -Wall -Wextra -Wshadow --std=c++20 -O2 tools/mem_trc/parse_log.cc -o parse_log

function run {
	mkdir log

	for ((i=1; i<=$1; i++))
	do
		time ./build/rv32emu ./build/$2.elf 2> journal.txt
		./parse_log journal.txt > log/$2.$3.$i.log
	done
}

function elf_name {
	# execute baseline
	git checkout mem_alloc/mpool_baseline
	make clean; make ENABLE_JIT=1
	run 50 $1 "baseline"

	# execute single TLSF
	git checkout mem_alloc/mpool_replacement_single
	make clean; make ENABLE_JIT=1
	run 50 $1 "single_tlsf"
}

elf_name "hello"
elf_name "coremark"
elf_name "captcha"
elf_name "chacha20"
elf_name "dhrystone"
elf_name "fcalc"
elf_name "hamilton"
elf_name "lena"
elf_name "nqueen"
