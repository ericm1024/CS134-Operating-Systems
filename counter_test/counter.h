// addapted from OSTEP chapter 29 figure 29.5
// http://pages.cs.wisc.edu/~remzi/OSTEP/threads-locks-usage.pdf
//
// NB: none of this is tested, it's just for exposition. Compile with
//     `gcc -Wall -Wextra -c counter.c`
// or otherwise 

#pragma once

#define _GNU_SOURCE // for sched_getcpu

#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <sched.h>

#define NUMCPUS 64

struct padded_ulong {
        unsigned long data;
        char pad[64 - sizeof(unsigned long)];
};

struct global_atomic_counter {
        // global count
        struct padded_ulong global;

        // local count (per cpu)
        struct padded_ulong local[NUMCPUS];

        // update frequency
        unsigned threshold;
};

// zero everything and set the threshold
static inline void
init_gc_atomic(struct global_atomic_counter *c, unsigned threshold)
{
        memset(c, 0, sizeof *c);
        c->threshold = threshold;
}

// update: atomically increment our local counter, then atomically incrememt
// the global counter if we crossed a threshold (or several thresholds)
static inline void
update_gc_atomic(struct global_atomic_counter *c, unsigned long amt)
{
        int cpu = sched_getcpu();
        assert(cpu >= 0);

        unsigned long old = __sync_fetch_and_add(&c->local[cpu].data, amt);
        unsigned long new = old + amt;

        unsigned long thresh = c->threshold;
        unsigned long threadholds_crossed = new/thresh - old/thresh;

        // transfer to global
        if (threadholds_crossed > 0)
                __sync_fetch_and_add(&c->global.data,
                                     thresh*threadholds_crossed);
}

// get: just return global amount (which may not be perfect)
static inline unsigned long
get_gc_atomic(struct global_atomic_counter *c)
{
        // see http://stackoverflow.com/a/24149439/3775803
        return __sync_fetch_and_add(&c->global.data, 0);
}

// *** SYNC UNSAFE FOR TESTING ONLY ****
static inline void
finalize_gc_atomic(struct global_atomic_counter *c)
{
        for (size_t i = 0; i < NUMCPUS; ++i)
                c->global.data += c->local[i].data%c->threshold;
}

struct global_mutex_counter {
        pthread_mutex_t global_lock;
        unsigned long global;

        unsigned long locals[NUMCPUS];
        pthread_mutex_t local_locks[NUMCPUS];

        unsigned long threshold;
};

// zero everything and set the threshold
static inline void
init_gc_mutex(struct global_mutex_counter *c, unsigned threshold)
{
        pthread_mutex_init(&c->global_lock, NULL);
        c->global = 0;

        for (size_t i = 0; i < NUMCPUS; ++i) {
                c->locals[i] = 0;
                pthread_mutex_init(&c->local_locks[i], NULL);
        }
        c->threshold = threshold;
}

// update: atomically increment our local counter, then atomically incrememt
// the global counter if we crossed a threshold (or several thresholds)
static inline void
update_gc_mutex(struct global_mutex_counter *c, unsigned long amt)
{
        unsigned idx = pthread_self() % NUMCPUS;
        pthread_mutex_lock(&c->local_locks[idx]);
        c->locals[idx] += amt;
        if (c->locals[idx] >= c->threshold) {
                pthread_mutex_lock(&c->global_lock);
                c->global += c->locals[idx];
                pthread_mutex_unlock(&c->global_lock);
                c->locals[idx] = 0;
        }
        pthread_mutex_unlock(&c->local_locks[idx]);
}

// get: just return global amount (which may not be perfect)
static inline unsigned long
get_gc_mutex(struct global_mutex_counter *c)
{
        unsigned long ret;
        pthread_mutex_lock(&c->global_lock);
        ret = c->global;
        pthread_mutex_unlock(&c->global_lock);
        return ret;
}

// *** SYNC UNSAFE FOR TESTING ONLY ****
static inline void
finalize_gc_mutex(struct global_mutex_counter *c)
{
        for (size_t i = 0; i < NUMCPUS; ++i)
                c->global += c->locals[i];
}
