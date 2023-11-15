#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "riscv.h"

const int max_cycles = 5000;
const char *fake_rv32emu_name = "./fake_rv32emu";
const char *fake_elf_name = "fake_elf";

/* In order to be able to inspect a coredump we want to crash on every ASAN
 * error.
 */
extern "C" void __asan_on_error()
{
    abort();
}
extern "C" void __msan_on_error()
{
    abort();
}

static void fuzz_elf_loader(const uint8_t *data, size_t len)
{
    // TODO
}

extern "C" void LLVMFuzzerTestOneInput(const uint8_t *data, size_t len)
{
    fuzz_elf_loader(data, len);
}
