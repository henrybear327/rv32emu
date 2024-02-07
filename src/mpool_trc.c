#include "mpool_trc.h"
#include <time.h>
#include <unistd.h>

void *malloc_with_trace(size_t __size)
{
    // fprintf(stderr, "malloc_with_trace()\n");

    // begin time
    clock_t start_t = clock();

// use original malloc
#undef malloc
    void *ptr = malloc(__size);
#define malloc malloc_with_trace

    // end time
    clock_t end_t = clock();

    // write log to the buffer
    // double total_t = (double) (end_t - start_t) / CLOCKS_PER_SEC;
    clock_t total_t = end_t - start_t;
    fprintf(stderr, "a %zu %p %d %ld\n", __size, ptr, getpid(), total_t);

    return ptr;
}

void *calloc_with_trace(size_t __count, size_t __size)
{
    // fprintf(stderr, "calloc_with_trace()\n");

    // begin time
    clock_t start_t = clock();

// use original calloc
#undef calloc
    void *ptr = calloc(__count, __size);
#define calloc calloc_with_trace

    // end time
    clock_t end_t = clock();

    // write log to the buffer
    // double total_t = (double) (end_t - start_t) / CLOCKS_PER_SEC;
    clock_t total_t = end_t - start_t;
    fprintf(stderr, "a %zu %p %d %ld\n", __size, ptr, getpid(), total_t);

    return ptr;
}

void free_with_trace(void *ptr)
{
    // fprintf(stderr, "free_with_trace()\n");

    // begin time
    clock_t start_t = clock();

// use original free
#undef free
    free(ptr);
#define free free_with_trace

    // end time
    clock_t end_t = clock();

    // write log to the buffer
    // double total_t = (double) (end_t - start_t) / CLOCKS_PER_SEC;
    clock_t total_t = end_t - start_t;
    fprintf(stderr, "f %p %d %ld\n", ptr, getpid(), total_t);
}
