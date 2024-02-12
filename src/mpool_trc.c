#include "mpool_trc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "tlsf.h"
#define LOG_MALLOC 1

static tlsf t = TLSF_INIT;

void *malloc_with_trace(size_t __size)
{
    // fprintf(stderr, "malloc_with_trace()\n");

    // begin time
    struct timespec start, end;
    int err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    assert(err == 0);

// use original malloc
#undef malloc
    void *ptr = tlsf_malloc(&t, __size);
#define malloc malloc_with_trace

    // end time
    err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
    assert(err == 0);

    // write log to the buffer
    long total_t = (end.tv_sec - start.tv_sec) * 1000000000 +
                   (end.tv_nsec - start.tv_nsec);
#if LOG_MALLOC
    fprintf(stderr, "a %zu %p %d %ld\n", __size, ptr, getpid(), total_t);
#endif

    return ptr;
}

void *calloc_with_trace(size_t __count, size_t __size)
{
    // fprintf(stderr, "calloc_with_trace()\n");

    // begin time
    struct timespec start, end;
    int err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    assert(err == 0);

// use original calloc
#undef calloc
    void *ptr = tlsf_malloc(&t, __count * __size);
    memset(ptr, 0, __count * __size);
#define calloc calloc_with_trace

    // end time
    err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
    assert(err == 0);

    // write log to the buffer
    long total_t = (end.tv_sec - start.tv_sec) * 1000000000 +
                   (end.tv_nsec - start.tv_nsec);
#if LOG_MALLOC
    fprintf(stderr, "a %zu %p %d %ld\n", __size, ptr, getpid(), total_t);
#endif

    return ptr;
}

/**
 * Reference:
 * https://stackoverflow.com/questions/4636456/how-to-get-a-stack-trace-for-c-using-gcc-with-line-number-information
 */
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

void print_trace()
{
    char pid_buf[30];
    sprintf(pid_buf, "%d", getpid());
    char name_buf[512];
    name_buf[readlink("/proc/self/exe", name_buf, 511)] = 0;
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    int child_pid = fork();
    if (!child_pid) {
        dup2(2, 1);  // redirect output to stderr - edit: unnecessary?
        execl("/usr/bin/gdb", "gdb", "--batch", "-n", "-ex", "thread", "-ex",
              "bt", name_buf, pid_buf, NULL);
        abort(); /* If gdb failed to start */
    } else {
        waitpid(child_pid, NULL, 0);
    }
}
/* =============== */

void free_with_trace(void *ptr)
{
    // fprintf(stderr, "free_with_trace()\n");

    // if the ptr is NULL, there is nothing to free
    if (ptr == NULL) {
        /**
         * Potentially called from
         * - block_map_clear (rv=0xaaaae256f2c0) at src/riscv.c:58
         *
         * Need to change the Makefile
         * CFLAGS = -std=gnu99 -O2 -Wall -Wextra -> -g
         */
        // print_trace();

        return;
    }

    // begin time
    struct timespec start, end;
    int err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    assert(err == 0);

// use original free
#undef free
    tlsf_free(&t, ptr);
#define free free_with_trace

    // end time
    err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &end);
    assert(err == 0);

    // write log to the buffer
    long total_t = (end.tv_sec - start.tv_sec) * 1000000000 +
                   (end.tv_nsec - start.tv_nsec);
#if LOG_MALLOC
    fprintf(stderr, "f %p %d %ld\n", ptr, getpid(), total_t);
#endif
}

// mpool bridge

struct mpool {
    size_t chunk_size;
};

struct mpool *mpool_create(size_t pool_size, size_t chunk_size)
{
    // pool_size is unused since we are not actively maintaining a pool here
    struct mpool *ret = malloc_with_trace(sizeof(struct mpool));
    assert(ret);

    ret->chunk_size = chunk_size;

    return ret;
}

void *mpool_alloc(struct mpool *mp)
{
    return malloc_with_trace(mp->chunk_size);
}

void mpool_free(struct mpool *mp, void *target)
{
    // mp is unused since we are not actively maintaining a pool here
    free_with_trace(target);
}

void mpool_destroy(struct mpool *mp)
{
    free_with_trace(mp);
}
