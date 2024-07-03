/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */
#include "tlsf.h"
#include "mpool.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

static tlsf t = TLSF_INIT;

#define TIMER_INIT struct timespec start, end
#define TIMER_START(start) { \
    int err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start); \
    assert(err == 0); \
}
#define TIMER_END(end) { \
    int err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end); \
    assert(err == 0); \
}
#define TIMER_LOG_ALLOC(__size, ptr, start, end) { \
    long total_t = (end.tv_sec - start.tv_sec) * 1000000000 + \
                   (end.tv_nsec - start.tv_nsec); \
    fprintf(stderr, "a %zu %p %d %ld\n", __size, ptr, getpid(), total_t); \
}
#define TIMER_LOG_FREE(ptr, start, end) { \
    long total_t = (end.tv_sec - start.tv_sec) * 1000000000 + \
                   (end.tv_nsec - start.tv_nsec); \
    fprintf(stderr, "f %p %d %ld\n", ptr, getpid(), total_t); \
}

typedef struct mpool {
    size_t chunk_size;
} mpool_t;

mpool_t *mpool_create(size_t pool_size, size_t chunk_size)
{
    // TODO(henrybear327): remove unused variable pool_size
    mpool_t *new_mp = malloc(sizeof(mpool_t));
    if (!new_mp)
        return NULL;

    new_mp->chunk_size = chunk_size;

    return new_mp;
}

void *mpool_alloc(mpool_t *mp)
{
    TIMER_INIT;
    TIMER_START(start);
    void* ptr = tlsf_malloc(&t, mp->chunk_size);
    TIMER_END(end);
    TIMER_LOG_ALLOC(mp->chunk_size, ptr, start, end);
    return ptr;
}

void *mpool_calloc(mpool_t *mp)
{
    TIMER_INIT;
    TIMER_START(start);
    void* ptr = tlsf_malloc(&t, mp->chunk_size);
    TIMER_END(end);
    TIMER_LOG_ALLOC(mp->chunk_size, ptr, start, end);
    memset(ptr, 0, sizeof(mp->chunk_size));
    return ptr;
}

void mpool_free(mpool_t *mp, void *target)
{
    // TODO(henrybear327): remove unused variable mp
    TIMER_INIT;
    TIMER_START(start);
    tlsf_free(&t, target);
    TIMER_END(end);
    TIMER_LOG_FREE(target, start, end);
}

// TODO(henrybear327): remove unused function mpool_destroy
void mpool_destroy(mpool_t *mp)
{
    // no-op
    return;
}
