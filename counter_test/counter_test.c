// usage: ./counter_test <a,m> count nthreads
// 
// first arg is "a" for atomic or "m" for mutex implementation.
// second arg is number of times to increment each counter
// third arg is the number of threads to use
//
// threshold is fixed at 1000

#include "counter.h"
#include <stdio.h>
#include <stdlib.h>

static struct global_atomic_counter ac;
static struct global_mutex_counter mc;

static void *a_thread(void *arg)
{
        unsigned long count = (unsigned long)arg;
        for (size_t i = 0; i < count; ++i)
                update_gc_atomic(&ac, 1);
        return NULL;
}

static void *m_thread(void *arg)
{
        unsigned long count = (unsigned long)arg;
        for (size_t i = 0; i < count; ++i)
                update_gc_mutex(&mc, 1);
        return NULL;
}

int main(int argc, char **argv)
{
        unsigned threshold = 1000;
        
        if (argc != 4)
                exit(2);

        int atomic = 0;
        if (strcmp(argv[1], "a") == 0)
                atomic = 1;
        else if (strcmp(argv[1], "m") != 0)
                exit(2);

        if (atomic)
                init_gc_atomic(&ac, threshold);
        else
                init_gc_mutex(&mc, threshold);

        unsigned long count = atol(argv[2]);
        unsigned long nthreads = atol(argv[3]);
        
        pthread_t *threads = malloc(nthreads * sizeof *threads);
        assert(threads);        

        for (size_t i = 0; i < nthreads; ++i) {
                if (atomic)
                        pthread_create(threads + i, NULL, a_thread,
                                       (void*)count);
                else
                        pthread_create(threads + i, NULL, m_thread,
                                       (void*)count);
        }

        for (size_t i = 0; i < nthreads; ++i)
                pthread_join(threads[i], NULL);

        unsigned long final_count;
        if (atomic) {
                finalize_gc_atomic(&ac);
                final_count = ac.global.data;
        } else {
                finalize_gc_mutex(&mc);
                final_count = mc.global;
        }
        assert(final_count == nthreads * count);
}
