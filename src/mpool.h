/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

/* Fixed-size memory pool allocator.
 *
 * Provides O(1) allocation/free for fixed-size objects. Uses mmap with
 * demand paging when available, falling back to malloc. Pools auto-extend
 * when exhausted. All functions are NULL-safe.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

struct mpool;

/* Statistics structure for benchmarking */
typedef struct mpool_stats {
    /* Operation counts */
    size_t alloc_count;
    size_t free_count;
    size_t extend_count;
    /* Cumulative time in nanoseconds */
    uint64_t alloc_time_ns;
    uint64_t free_time_ns;
    uint64_t extend_time_ns;
} mpool_stats_t;

/**
 * mpool_create - create a memory pool
 * @pool_size: initial pool size in bytes
 * @chunk_size: size of each allocation unit
 *
 * Returns pointer to pool, or NULL on failure.
 */
struct mpool *mpool_create(size_t pool_size, size_t chunk_size);

/**
 * mpool_alloc - allocate a chunk from the pool
 * @mp: memory pool (NULL-safe)
 *
 * Returns pointer to chunk, or NULL if mp is NULL or allocation fails.
 */
void *mpool_alloc(struct mpool *mp);

/**
 * mpool_calloc - allocate a zero-initialized chunk from the pool
 * @mp: memory pool (NULL-safe)
 *
 * Returns pointer to zeroed chunk, or NULL if mp is NULL or allocation fails.
 */
void *mpool_calloc(struct mpool *mp);

/**
 * mpool_free - return a chunk to the pool
 * @mp: memory pool (NULL-safe)
 * @target: chunk to free (NULL-safe)
 */
void mpool_free(struct mpool *mp, void *target);

/**
 * mpool_destroy - destroy pool and release all memory
 * @mp: memory pool (NULL-safe)
 */
void mpool_destroy(struct mpool *mp);

/**
 * mpool_get_stats - get pool statistics
 * @mp: memory pool (NULL-safe)
 * @stats: output statistics structure (NULL-safe)
 */
void mpool_get_stats(struct mpool *mp, mpool_stats_t *stats);
