#include "mpool_trc.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define LOG_MALLOC 1

void *malloc_with_trace(size_t __size)
{
    // fprintf(stderr, "malloc_with_trace()\n");

    // begin time
    struct timespec start, end;
    int err = clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    assert(err == 0);

// use original malloc
#undef malloc
    void *ptr = malloc(__size);
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
    void *ptr = calloc(__count, __size);
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
    free(ptr);
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
