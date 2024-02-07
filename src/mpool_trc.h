/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */
#pragma once

/**
 * This is a hack.
 * 
 * In order to redirect the malloc, calloc, and free functions
 * calls to our proxy functions so we can add logging capability, 
 * we use define/undef extensively to workaround it. 
 * 
 * An attempt was made to use dlsym(RTLD_NEXT, "malloc"), but 
 * the code always segfaulted.
 */

#include <stdio.h>
#include <stdlib.h>

// forward declaration for define
void *malloc_with_trace(size_t __size);
void *calloc_with_trace(size_t __count, size_t __size);
void free_with_trace(void *ptr);

#define malloc malloc_with_trace
#define calloc calloc_with_trace
#define free free_with_trace
