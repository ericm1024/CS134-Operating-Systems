// lab2_list.c
//
// NAME: Eric Mueller
// EMAIL: emueller@hmc.edu
// ID: 40160869

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

struct multi_list_bin {
        SortedList_t head;
        int spinlock;
        pthread_mutex_t mutex;
};

struct multi_list {
        struct multi_list_bin *lists;
        int nlists;
};

enum locking_type {
        MUTEX,
        SPIN,
        NONE
};

enum locking_type locking = NONE;
int opt_yield = 0;
static size_t niters = 1;
static size_t nlists = 1;
static struct multi_list list;

#define THREADS_OPT_RET 't'
#define ITERATIONS_OPT_RET 'i'
#define YIELD_OPT_RET 'y'
#define SYNC_OPT_RET 's'
#define LISTS_OPT_RET 'l'
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
        { // --lists
                .name = "lists",
                .has_arg = required_argument,
                .flag = NULL,
                .val = LISTS_OPT_RET
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

// this one is my code
static void timespec_accum(struct timespec *accum, struct timespec *add)
{
        if (accum) {
                accum->tv_nsec += add->tv_nsec;
                accum->tv_sec += add->tv_sec + accum->tv_nsec/100000000;
                accum->tv_nsec %= 100000000;
        }
}

// OSTEP chapter 28 page 8
static void spin_lock(int *lock, struct timespec *total)
{
        struct timespec before, after, diff;

        int err = clock_gettime(CLOCK_MONOTONIC_RAW, &before);
        if (err)
                die("clock_gettime in thread", errno);
        
        while (__sync_lock_test_and_set(lock, 1) == 1)
                ;

        err = clock_gettime(CLOCK_MONOTONIC_RAW, &after);
        if (err)
                die("clock_gettime in thread", errno);

        timespec_diff(&before, &after, &diff);
        timespec_accum(total, &diff);
}
static void spin_unlock(int *lock)
{
        __sync_lock_release(lock);
}

static void pthread_mutex_lock__and_time(pthread_mutex_t *m,
                                         struct timespec *total)
{
        struct timespec before, after, diff;

        int err = clock_gettime(CLOCK_MONOTONIC_RAW, &before);
        if (err)
                die("clock_gettime in thread", errno);

        pthread_mutex_lock(m);

        err = clock_gettime(CLOCK_MONOTONIC_RAW, &after);
        if (err)
                die("clock_gettime in thread", errno);

        timespec_diff(&before, &after, &diff);
        timespec_accum(total, &diff);
}

// http://stackoverflow.com/a/7666577/3775803
static unsigned long hash(const char *_str)
{
        const unsigned char *str = (unsigned char *)_str;
        unsigned long hash = 5381;
        int c;

        while ((c = *str++))
                hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

        return hash;
}

void __mlist_do_lock(struct multi_list *ml, int idx, enum locking_type lt,
                     struct timespec *total)
{
        switch (lt) {
        case MUTEX:
                pthread_mutex_lock__and_time(&ml->lists[idx].mutex, total);
                break;

        case SPIN:
                spin_lock(&ml->lists[idx].spinlock, total);
                break;

        case NONE:
        default:
                break;
        }
}

void __mlist_do_unlock(struct multi_list *ml, int idx, enum locking_type lt)
{
        switch (lt) {
        case MUTEX:
                pthread_mutex_unlock(&ml->lists[idx].mutex);
                break;

        case SPIN:
                spin_unlock(&ml->lists[idx].spinlock);
                break;

        case NONE:
        default:
                break;
        }
}

// I hate myself
int __mlist_idx(struct multi_list *ml, const char *key)
{
        return hash(key) % ml->nlists;
}
int mlist_idx(struct multi_list *ml, SortedListElement_t *element)
{
        return __mlist_idx(ml, element->key);
}

void mlist_insert(struct multi_list *ml, SortedListElement_t *element,
                  enum locking_type lt, struct timespec *total)
{
        int idx = mlist_idx(ml, element);
        __mlist_do_lock(ml, idx, lt, total);
        SortedList_insert(&ml->lists[idx].head, element);
        __mlist_do_unlock(ml, idx, lt);
}

int mlist_delete(struct multi_list *ml, SortedListElement_t *element,
                 enum locking_type lt, struct timespec *total)
{
        int idx = mlist_idx(ml, element);
        __mlist_do_lock(ml, idx, lt, total);
        int ret = SortedList_delete(element);
        __mlist_do_unlock(ml, idx, lt);
        return ret;
}

SortedListElement_t *mlist_lookup(struct multi_list *ml, const char *key,
                                  enum locking_type lt, struct timespec *total)
{
        int idx = __mlist_idx(ml, key);
        __mlist_do_lock(ml, idx, lt, total);
        SortedListElement_t *ret = SortedList_lookup(&ml->lists[idx].head,
                                                     key);
        __mlist_do_unlock(ml, idx, lt);
        return ret;
}

int mlist_length(struct multi_list *ml, enum locking_type lt,
                 struct timespec *total)
{
        int tlen = 0;
        for (int i = 0; i < ml->nlists; ++i) {
                __mlist_do_lock(ml, i, lt, total);
                int len = SortedList_length(&ml->lists[i].head);
                __mlist_do_unlock(ml, i, lt);
                if (len == -1)
                        return -1;
                tlen += len;
        }
        return tlen;
}

// generic thread
static void *thread(void *arg)
{
        SortedListElement_t *e = arg;
        struct timespec *total = NULL;

        if (locking != NONE) {
                total = calloc(1, sizeof *total);
                if (!total)
                        die("malloc", ENOMEM);
        }

        for (size_t i = 0; i < niters; ++i)
                mlist_insert(&list, e+i, locking, total);

        int len = mlist_length(&list, locking, total);
        if (len == -1) {
                // EIO is the catch-all error code for "something happened".
                errno = EIO;
                perror("thread: SortedList_length");
                exit(2);
        }
        
        for (size_t i = 0; i < niters; ++i) {
                SortedListElement_t *n = mlist_lookup(&list, e[i].key,
                                                      locking, total);
                if (n == NULL) {
                        errno = EIO;
                        perror("thread: SortedList_lookup");
                        exit(2);
                }

                int err = mlist_delete(&list, n, locking, total);
                if (err) {
                        errno = EIO;
                        perror("thread: SortedList_delete");
                        exit(2);
                }
        }

        return total;
}

int main(int argc, char **argv)
{
        size_t nthreads = 1;
        int ret, err, fd;
        unsigned int seed;
        struct timespec start, end, elapsed, mutex_wait_time;
        pthread_t *threads;
        const clockid_t clock = CLOCK_PROCESS_CPUTIME_ID;

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
                        } else if (strcmp(optarg, "s") == 0) {
                                locking = SPIN;
                        } else {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad --sync argument", EINVAL);
                        }
                        break;

                case LISTS_OPT_RET:
                        nlists = strtoll(optarg, NULL, 10);
                        if (errno) {
                                fprintf(stderr, USAGE_STR);
                                die("main: bad iteration count", errno);
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

        // initialize the global multi-list
        list.nlists = nlists;
        list.lists = calloc(nlists, sizeof *list.lists);
        if (!list.lists)
                die("malloc", ENOMEM);
        for (size_t i = 0; i < nlists; ++i) {
                struct multi_list_bin *l = list.lists + i;
                l->head.prev = &l->head;
                l->head.next = &l->head;
                l->spinlock = 0;
                pthread_mutex_init(&l->mutex, NULL);
        }

        memset(&mutex_wait_time, 0, sizeof mutex_wait_time);

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
                pthread_create(threads + i, NULL, thread, data + i*niters);

        for (size_t i = 0; i < nthreads; ++i) {
                void *ret;
                pthread_join(threads[i], &ret);

                // if we're running the mutex thread, we expect to get
                // the ammount of time we spend trying to lock as a result
                if (ret) {
                        timespec_accum(&mutex_wait_time, ret);
                        free(ret);
                }
        }

        err = clock_gettime(clock, &end);
        if (err)
                die("clock_gettime failed", errno);
        
        int len = mlist_length(&list, locking, NULL);
        if (len == -1)
                die("main: SortedList_length", EIO);

        timespec_diff(&start, &end, &elapsed);
        long long nsec = elapsed.tv_nsec
                         + 1000000000 * (long long)elapsed.tv_sec;
        const size_t nops = 3*nthreads*niters;
        long long mutex_nsec = mutex_wait_time.tv_nsec
                + 1000000000 * (long long)mutex_wait_time.tv_sec;
        const size_t mutex_nops = nthreads*(3*niters + 1);
        printf("list-%s%s%s%s-%s,%lu,%lu,%lu,%lu,%lld,%lld,%lld\n",
               opt_yield ? "" : "none",
               opt_yield & INSERT_YIELD ? "i" : "",
               opt_yield & DELETE_YIELD ? "d" : "",
               opt_yield & LOOKUP_YIELD ? "l" : "",
               locking == MUTEX ? "m"
               : locking == SPIN ? "s"
               : "none",
               nthreads,
               niters,
               nlists,
               nops,
               nsec,
               nsec/nops,
               mutex_nsec/mutex_nops);
}
