/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* TLSF-backed memory pool implementation.
 *
 * Provides the same mpool API using TLSF (Two-Level Segregated Fit)
 * allocator internally. TLSF provides O(1) allocation and deallocation.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if HAVE_MMAP
#include <sys/mman.h>
#include <unistd.h>
#else
#define getpagesize() 4096
#endif

#include "mpool.h"
#include "tlsf/tlsf.h"

typedef struct mpool {
    tlsf_t tlsf;
    size_t chunk_size;
    void *memory;
    size_t memory_size;
    size_t current_size;
    /* Statistics */
    size_t alloc_count;
    size_t free_count;
    size_t extend_count;
    uint64_t alloc_time_ns;
    uint64_t free_time_ns;
    uint64_t extend_time_ns;
} mpool_t;

/* Thread-local storage for the current pool being operated on.
 * This is needed because tlsf_resize callback doesn't have a user context.
 */
static __thread mpool_t *current_pool = NULL;

static inline uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Allocate backing memory via mmap or malloc */
static void *mem_arena(size_t sz)
{
    void *p;
#if HAVE_MMAP
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
    p = mmap(0, sz, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
        return NULL;
#else
    p = malloc(sz);
    if (!p)
        return NULL;
#endif
    return p;
}

static void mem_arena_free(void *p, size_t sz)
{
#if HAVE_MMAP
    munmap(p, sz);
#else
    (void) sz;
    free(p);
#endif
}

/* TLSF resize callback - called by TLSF to request memory.
 * We use pre-allocated memory, so just return it if the request fits.
 */
void *tlsf_resize(tlsf_t *t, size_t size)
{
    (void) t;

    if (!current_pool || !current_pool->memory)
        return NULL;

    if (size > current_pool->memory_size)
        return NULL;

    current_pool->current_size = size;
    return current_pool->memory;
}

mpool_t *mpool_create(size_t pool_size, size_t chunk_size)
{
    mpool_t *mp = malloc(sizeof(mpool_t));
    if (!mp)
        return NULL;

    memset(mp, 0, sizeof(*mp));
    mp->chunk_size = chunk_size;

    /* Allocate significantly more memory than requested to allow for growth.
     * TLSF doesn't auto-extend like the default mpool, so we pre-allocate
     * extra space. With mmap and MAP_NORESERVE, this is just virtual address
     * space - physical pages are only committed when accessed.
     */
    size_t expanded_size = pool_size * 16;
    if (expanded_size < (1 << 20)) /* Minimum 1MB */
        expanded_size = 1 << 20;

    /* Round up to page size */
    size_t pgsz = getpagesize();
    mp->memory_size = (expanded_size + pgsz - 1) & ~(pgsz - 1);

    mp->memory = mem_arena(mp->memory_size);
    if (!mp->memory) {
        free(mp);
        return NULL;
    }

    /* Initialize TLSF - set current_pool for the resize callback */
    mp->tlsf = (tlsf_t) TLSF_INIT;
    current_pool = mp;

    /* Trigger TLSF to initialize with our memory by doing a dummy alloc/free */
    void *dummy = tlsf_malloc(&mp->tlsf, 16);
    if (!dummy) {
        mem_arena_free(mp->memory, mp->memory_size);
        free(mp);
        current_pool = NULL;
        return NULL;
    }
    tlsf_free(&mp->tlsf, dummy);

    current_pool = NULL;
    return mp;
}

void *mpool_alloc(mpool_t *mp)
{
    if (!mp)
        return NULL;

    uint64_t start = get_time_ns();

    current_pool = mp;
    void *ptr = tlsf_malloc(&mp->tlsf, mp->chunk_size);
    current_pool = NULL;

    mp->alloc_time_ns += get_time_ns() - start;

    if (ptr)
        mp->alloc_count++;
    return ptr;
}

void *mpool_calloc(mpool_t *mp)
{
    if (!mp)
        return NULL;

    uint64_t start = get_time_ns();

    current_pool = mp;
    void *ptr = tlsf_malloc(&mp->tlsf, mp->chunk_size);
    current_pool = NULL;

    mp->alloc_time_ns += get_time_ns() - start;

    if (ptr) {
        memset(ptr, 0, mp->chunk_size);
        mp->alloc_count++;
    }
    return ptr;
}

void mpool_free(mpool_t *mp, void *target)
{
    if (!mp || !target)
        return;

    uint64_t start = get_time_ns();

    current_pool = mp;
    tlsf_free(&mp->tlsf, target);
    current_pool = NULL;

    mp->free_time_ns += get_time_ns() - start;
    mp->free_count++;
}

void mpool_destroy(mpool_t *mp)
{
    if (!mp)
        return;

    mem_arena_free(mp->memory, mp->memory_size);
    free(mp);
}

void mpool_get_stats(struct mpool *mp, mpool_stats_t *stats)
{
    if (!mp || !stats)
        return;
    stats->alloc_count = mp->alloc_count;
    stats->free_count = mp->free_count;
    stats->extend_count = mp->extend_count;
    stats->alloc_time_ns = mp->alloc_time_ns;
    stats->free_time_ns = mp->free_time_ns;
    stats->extend_time_ns = mp->extend_time_ns;
}
