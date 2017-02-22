// lab2_add.c
//
// Eric Mueller -- emueller@hmc.edu
// 
// Driver for synchronization tests for project 2A for cs134, described here:
// http://www.cs.pomona.edu/classes/cs134/projects/P2A.html

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define THREADS_OPT_RET 't'
#define ITERATIONS_OPT_RET 'i'
#define YIELD_OPT_RET 'y'
#define SYNC_OPT_RET 's'
#define USAGE_STR \
"lab2_add [--threads=n] [--iterations=n] [--yield] [--sync={m,s,c,a}]"

static const struct option lab2_opts[] =
{
        { // --threads=<number>
                .name = "threads",
                .has_arg = required_argument,
                .flag = NULL,
                .val = THREADS_OPT_RET,
        },
        { // --iterations=<number>
                .name = "iterations",
                .has_arg = required_argument,
                .flag = NULL,
                .val = ITERATIONS_OPT_RET
        },
        { // --yield
                .name = "yield",
                .has_arg = no_argument,
                .flag = NULL,
                .val = YIELD_OPT_RET
        },
        { // --sync
                .name = "sync",
                .has_arg = required_argument,
                .flag = NULL,
                .val = SYNC_OPT_RET
        },
        {0, 0, 0, 0} // end of array
};

// exit, possibly with a message
static void die(const char *reason, int err)
{
        if (reason) {
                fprintf(stderr, "%s: %s\n", reason, strerror(err));
                exit(1);
        } else {
                exit(0);
        }
}

// OSTEP chapter 28 page 8
static void spin_lock(int *lock)
{
        while (__sync_lock_test_and_set(lock, 1) == 1)
                ;
}
static void spin_unlock(int *lock)
{
        __sync_lock_release(lock);
}

static int opt_yield = 0;
static void add(long long *pointer, long long value)
{
        long long sum = *pointer + value;
        if (opt_yield)
                sched_yield();
        *pointer = sum;
}

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void add_m(long long *pointer, long long value)
{
        int err;

        err = pthread_mutex_lock(&mutex);
        assert(err == 0);

        long long sum = *pointer + value;
        if (opt_yield)
                sched_yield();
        *pointer = sum;

        err = pthread_mutex_unlock(&mutex);
        assert(err == 0);
}

static int lock = 0;
static void add_s(long long *pointer, long long value)
{
        spin_lock(&lock);
        long long sum = *pointer + value;
        if (opt_yield)
                sched_yield();
        *pointer = sum;
        spin_unlock(&lock);
}

static void add_c(long long *ptr, long long value)
{
        long long old, new;
        do {
                old = *ptr;
                new = old + value;

                if (opt_yield)
                        sched_yield();
        } while (!__sync_bool_compare_and_swap(ptr, old, new));
}

// use an atomic add
static void add_a(long long *ptr, long long value)
{
        __sync_fetch_and_add(ptr, value);
}

// not my code. Stolen from https://gist.github.com/diabloneo/9619917
static void timespec_diff(struct timespec *start, struct timespec *stop,
                          struct timespec *result)
{
        if ((stop->tv_nsec - start->tv_nsec) < 0) {
                result->tv_sec = stop->tv_sec - start->tv_sec - 1;
                result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
        } else {
                result->tv_sec = stop->tv_sec - start->tv_sec;
                result->tv_nsec = stop->tv_nsec - start->tv_nsec;
        }
}

static long long counter = 0;
static size_t niters = 1;

typedef void (*add_func_t)(long long*, long long);

static void *thread(void *arg)
{
        add_func_t f = (add_func_t)arg;
        
        for (size_t i = 0; i < niters; ++i)
                f(&counter, 1);

        for (size_t i = 0; i < niters; ++i)
                f(&counter, -1);

        return NULL;
}

enum locking_type {
        MUTEX,
        SPIN,
        CAS,
        ATOMIC,
        NONE
};

int main(int argc, char **argv)
{
        size_t nthreads = 1;
        int ret, err;
        struct timespec start, end, elapsed;
        pthread_t *threads;
        const clockid_t clock = CLOCK_PROCESS_CPUTIME_ID;//CLOCK_MONOTONIC_RAW;
        add_func_t thread_func = add;
        enum locking_type locking = NONE;

        while (-1 != (ret = getopt_long(argc, argv, "", lab2_opts, NULL))) {
                switch (ret) {
                case THREADS_OPT_RET:
                        errno = 0;
                        nthreads = strtoll(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad thread count", errno);
                        }
                        break;

                case ITERATIONS_OPT_RET:
                        errno = 0;
                        niters = strtoll(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad iteration count", errno);
                        }
                        break;

                case YIELD_OPT_RET:
                        opt_yield = 1;
                        break;

                case SYNC_OPT_RET:
                        if (strcmp(optarg, "m") == 0) {
                                locking = MUTEX;
                                thread_func = add_m;
                        } else if (strcmp(optarg, "s") == 0) {
                                locking = SPIN;
                                thread_func = add_s;
                        } else if (strcmp(optarg, "c") == 0) {
                                locking = CAS;
                                thread_func = add_c;
                        } else if (strcmp(optarg, "a") == 0) {
                                locking = ATOMIC;
                                thread_func = add_a;
                        } else {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad --sync argument", EINVAL);
                        }
                        break;

                default:
                        exit(1);
                }

        }

        if (locking == ATOMIC && opt_yield) {
                fprintf(stderr, USAGE_STR);
                die("main: can not support --yield with --sync=a", EINVAL);
        }

        threads = calloc(nthreads, sizeof *threads);
        if (!threads)
                die("calloc", ENOMEM);

        err = clock_gettime(clock, &start);
        if (err)
                die("clock_gettime failed", errno);

        for (size_t i = 0; i < nthreads; ++i)
                pthread_create(threads + i, NULL, thread, (void*)thread_func);

        for (size_t i = 0; i < nthreads; ++i)
                pthread_join(threads[i], NULL);

        err = clock_gettime(clock, &end);
        if (err)
                die("clock_gettime failed", errno);

        timespec_diff(&start, &end, &elapsed);
        long long nsec = elapsed.tv_nsec + 1000000000 * (long long)elapsed.tv_sec;
        const size_t nops = 2*nthreads*niters;
        printf("%s%s%s,%lu,%lu,%lu,%lld,%lld,%lld\n",
               "add",
               opt_yield ? "-yield" : "",
               locking == MUTEX ? "-m"
               : locking == SPIN ? "-s"
               : locking == CAS ? "-c"
               : locking == ATOMIC ? "-a"
               : "-none",
               nthreads,
               niters,
               2*nthreads*niters,
               nsec,
               nsec/nops,
               counter);
}
