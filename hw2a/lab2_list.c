// lab2_list.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869
// 
// Driver for synchronization tests for project 2A for cs134, described here:
// http://www.cs.pomona.edu/classes/cs134/projects/P2A.html

#include "SortedList.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>

int opt_yield = 0;
static size_t niters = 1;
static SortedList_t head;

#define THREADS_OPT_RET 't'
#define ITERATIONS_OPT_RET 'i'
#define YIELD_OPT_RET 'y'
#define SYNC_OPT_RET 's'
#define USAGE_STR \
"lab2_add [--threads=n] [--iterations=n] [--yield={idl}] [--sync={m,s}]\n"

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
        { // --yield=[idl]
                .name = "yield",
                .has_arg = required_argument,
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

static void seg_handler(int arg)
{
        (void)arg;

        fprintf(stderr, "%s: %s\n", __func__, strerror(EFAULT));
        exit(2);
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

typedef void *(*thread_func_t)(void *arg);

// spinlock-using thread
static int lock;
static void *s_thread(void *arg)
{
        SortedListElement_t *e = arg;

        for (size_t i = 0; i < niters; ++i) {
                spin_lock(&lock);
                SortedList_insert(&head, e+i);
                spin_unlock(&lock);
        }

        spin_lock(&lock);
        int len = SortedList_length(&head);
        spin_unlock(&lock);

        if (len == -1) {
                // EIO is the catch-all error code for "something happened".
                errno = EIO;
                perror("s_thread: SortedList_length");
                exit(2);
        }

        for (size_t i = 0; i < niters; ++i) {
                spin_lock(&lock);
                SortedListElement_t *n = SortedList_lookup(&head, e[i].key);
                spin_unlock(&lock);
                if (n == NULL) {
                        errno = EIO;
                        perror("s_thread: SortedList_lookup");
                        exit(2);
                }

                spin_lock(&lock);
                int err = SortedList_delete(n);
                spin_unlock(&lock);
                if (err) {
                        errno = EIO;
                        perror("s_thread: SortedList_delete");
                        exit(2);
                }
        }

        return NULL;
}

// mutex using thread
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void *m_thread(void *arg)
{
        SortedListElement_t *e = arg;

        for (size_t i = 0; i < niters; ++i) {
                pthread_mutex_lock(&mutex);
                SortedList_insert(&head, e+i);
                pthread_mutex_unlock(&mutex);
        }

        pthread_mutex_lock(&mutex);
        int len = SortedList_length(&head);
        pthread_mutex_unlock(&mutex);

        if (len == -1) {
                errno = EIO;
                perror("m_thread: SortedList_length");
                exit(2);
        }

        for (size_t i = 0; i < niters; ++i) {
                pthread_mutex_lock(&mutex);
                SortedListElement_t *n = SortedList_lookup(&head, e[i].key);
                pthread_mutex_unlock(&mutex);
                if (n == NULL) {
                        errno = EIO;
                        perror("m_thread: SortedList_lookup");
                        exit(2);
                }

                pthread_mutex_lock(&mutex);
                int err = SortedList_delete(n);
                pthread_mutex_unlock(&mutex);
                if (err) {
                        errno = EIO;
                        perror("m_thread: SortedList_delete");
                        exit(2);
                }
        }

        return NULL;
}

// unsynchronized thread
static void *n_thread(void *arg)
{
        SortedListElement_t *e = arg;

        for (size_t i = 0; i < niters; ++i)
                SortedList_insert(&head, e+i);

        int len = SortedList_length(&head);
        if (len == -1) {
                errno = EIO;
                perror("n_thread: SortedList_length");
                exit(2);
        }

        for (size_t i = 0; i < niters; ++i) {
                SortedListElement_t *n = SortedList_lookup(&head, e[i].key);
                if (n == NULL) {
                        errno = EIO;
                        perror("n_thread: SortedList_lookup");
                        exit(2);
                }

                int err = SortedList_delete(n);
                if (err) {
                        errno = EIO;
                        perror("n_thread: SortedList_delete");
                        exit(2);
                }
        }

        return NULL;
}

enum locking_type {
        MUTEX,
        SPIN,
        NONE
};

int main(int argc, char **argv)
{
        size_t nthreads = 1;
        int ret, err, fd;
        unsigned int seed;
        struct timespec start, end, elapsed;
        pthread_t *threads;
        const clockid_t clock = CLOCK_PROCESS_CPUTIME_ID;
        thread_func_t thread_func = n_thread;
        enum locking_type locking = NONE;

        while (-1 != (ret = getopt_long(argc, argv, "", lab2_opts, NULL))) {
                switch (ret) {
                case THREADS_OPT_RET:
                        errno = 0;
                        nthreads = strtoll(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad thread count", errno);
                        } else if (nthreads > 255) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad thread count", EINVAL);
                        }
                        break;

                case ITERATIONS_OPT_RET:
                        errno = 0;
                        niters = strtoll(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad iteration count", errno);
                        } else if (niters > (1 << 30)) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad iteration count", EINVAL);
                        }
                        break;

                case YIELD_OPT_RET:
                        // lol.
                        for (size_t i = 0; i < strlen(optarg); ++i) {
                                if (optarg[i] == 'i') {
                                        if (opt_yield & INSERT_YIELD) {
                                                fprintf(stderr, USAGE_STR);
                                                die("bad --yield", EINVAL);
                                        }
                                        opt_yield |= INSERT_YIELD;
                                } else if (optarg[i] == 'd') {
                                        if (opt_yield & DELETE_YIELD) {
                                                fprintf(stderr, USAGE_STR);
                                                die("bad --yield", EINVAL);
                                        }
                                        opt_yield |= DELETE_YIELD;
                                } else if (optarg[i] == 'l') {
                                        if (opt_yield & LOOKUP_YIELD) {
                                                fprintf(stderr, USAGE_STR);
                                                die("bad --yield", EINVAL);
                                        }
                                        opt_yield |= LOOKUP_YIELD;
                                } else {
                                        fprintf(stderr, USAGE_STR);
                                        die("bad --yield", EINVAL);
                                }
                        }
                        break;

                case SYNC_OPT_RET:
                        if (strcmp(optarg, "m") == 0) {
                                locking = MUTEX;
                                thread_func = m_thread;
                        } else if (strcmp(optarg, "s") == 0) {
                                locking = SPIN;
                                thread_func = s_thread;
                        } else {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad --sync argument", EINVAL);
                        }
                        break;

                default:
                        exit(1);
                }

        }

        // srand() with something better than time(NULL)
        fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0)
                die("can't open /dev/urandom", errno);
        err = read(fd, &seed, sizeof seed);
        if (err < 0)
                die("can't read from /dev/urandom", errno);
        srand(seed);

        // allocate some memory for the list elements
        SortedListElement_t *data = calloc(nthreads*niters, sizeof *data);
        if (!data)
                die("malloc", ENOMEM);

        // initialize each key with a random 64 byte key;
        const size_t ksize = 64;
        for (size_t i = 0; i < nthreads*niters; ++i) {
                // malloc a key
                char *key = calloc(ksize, 1);
                if (!key)
                        die("malloc", ENOMEM);

                // fill in the key with some random non-zero data, but leave
                // the last byte as zero
                for (char *c = key; c != key + (ksize - 1); ++c)
                        do {
                                *c = rand();
                        } while (*c == 0);

                data[i].key = (const char *)key;
        }

        head.prev = &head;
        head.next = &head;

        threads = calloc(nthreads, sizeof *threads);
        if (!threads)
                die("malloc", ENOMEM);

        // set up segfault handler
        if (signal(SIGSEGV, seg_handler) == SIG_ERR)
                die("signal", errno);
        
        err = clock_gettime(clock, &start);
        if (err)
                die("clock_gettime failed", errno);

        for (size_t i = 0; i < nthreads; ++i)
                pthread_create(threads + i, NULL, thread_func,
                               data + i*niters);

        for (size_t i = 0; i < nthreads; ++i)
                pthread_join(threads[i], NULL);

        err = clock_gettime(clock, &end);
        if (err)
                die("clock_gettime failed", errno);
        
        int len = SortedList_length(&head);
        if (len == -1)
                die("main: SortedList_length", EIO);

        timespec_diff(&start, &end, &elapsed);
        long long nsec = elapsed.tv_nsec
                         + 1000000000 * (long long)elapsed.tv_sec;
        const size_t nops = 3*nthreads*niters;
        printf("list-%s%s%s%s-%s,%lu,%lu,1,%lu,%lld,%lld\n",
               opt_yield ? "" : "none",
               opt_yield & INSERT_YIELD ? "i" : "",
               opt_yield & DELETE_YIELD ? "d" : "",
               opt_yield & LOOKUP_YIELD ? "l" : "",
               locking == MUTEX ? "m"
               : locking == SPIN ? "s"
               : "none",
               nthreads,
               niters,
               nops,
               nsec,
               nsec/nops);
}
