#include "mpool_trc.h"

void *malloc_with_trace(size_t __size)
{
    fprintf(stderr, "my malloc()\n");

// use original malloc
#undef malloc
    return malloc(__size);
#define malloc malloc_with_trace
}

void *calloc_with_trace(size_t __count, size_t __size)
{
    fprintf(stderr, "my calloc()\n");

// use original calloc
#undef calloc
    return calloc(__count, __size);
#define calloc calloc_with_trace
}

void free_with_trace(void *ptr)
{
    fprintf(stderr, "my free()\n");

// use original free
#undef free
    free(ptr);
#define free free_with_trace
}
