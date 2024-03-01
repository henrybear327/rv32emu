/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */
#include "tlsf.h"
#include "mpool.h"
#include <stdlib.h>
#include <string.h>

static tlsf t = TLSF_INIT;

typedef struct mpool {
    size_t chunk_size;
} mpool_t;

mpool_t *mpool_create(size_t pool_size, size_t chunk_size)
{
    mpool_t *new_mp = malloc(sizeof(mpool_t));
    if (!new_mp)
        return NULL;

    new_mp->chunk_size = chunk_size;

    return new_mp;
}

void *mpool_alloc(mpool_t *mp)
{
    return tlsf_malloc(&t, mp->chunk_size);
}

void *mpool_calloc(mpool_t *mp)
{
    void* ptr = tlsf_malloc(&t, mp->chunk_size);
    memset(ptr, 0, sizeof(mp->chunk_size));
    return ptr;
}

void mpool_free(mpool_t *mp, void *target)
{
    tlsf_free(&t, target);
}

void mpool_destroy(mpool_t *mp)
{
    // no-op
    return;
}
