/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

#define NUM_THREADS 5

struct mux_sum {
    pthread_mutex_t* mux;
    int* sum;
};

static void* _test_thread_returnOne() {
    int* retval = (int*)malloc(sizeof(int));
    *retval = 1;
    return retval;
}

static void* _test_thread_muxlock(void* data) {
    intptr_t retval = 0;

    struct mux_sum* ms;
    ms = (struct mux_sum*)data;
    /* lock the mutex. If we weren't the first here, block */
    if(pthread_mutex_lock(ms->mux) < 0) {
        fprintf(stdout, "error: pthread_mutex_lock failed\n");
        retval = -1;
    }

    int current = *(ms->sum);
    if(current == 0) {
        *(ms->sum) = 2;
    }
    else {
        *(ms->sum) = current*current;
    }

    /* unlock the mutex, let the next guy come through */
    if(pthread_mutex_unlock(ms->mux) < 0) {
        fprintf(stdout, "error: pthread_mutex_unlock failed\n");
        retval = -1;
    }

    return (void*)retval;
}

static void* _test_thread_muxtrylock(void* mx) {
    /* Track the number of threads that pass the lock. Should be < NUM_THREADS */
    intptr_t counter = 0;
    pthread_mutex_t* mux = (pthread_mutex_t*)mx;

    /* Attempt to lock the mutex */
    if (pthread_mutex_trylock(mux) == 0) {
        /* some busy work... Probably a better way TODO this */
        int i;
        for(i=0; i<(1000000/NUM_THREADS); i++){
            int junk = i*i;
        }

        if(pthread_mutex_unlock(mux) < 0) {
            fprintf(stdout, "error: pthread_mutex_unlock failed\n");
            counter = -1;
        }
        
        counter = 1;
    }

    return (void*)counter;
}

static int _test_joinThreads(pthread_t* threads) {
    int* retval=NULL;
    long t;
    for(t=0; t<NUM_THREADS; t++) {
        pthread_join(threads[t], (void**)&retval);
        if(retval == NULL) {
            fprintf(stdout, "error: pthread_join returned NULL pointer!\n");
            return -1;
        }
        if(*retval != 1) {
            fprintf(stdout, "error: pthread_join did not return 1!\n");
            return -1;
        }
        free(retval);
    }

    /* success! */
    return 0;
}
static int _test_makeJoinable(pthread_t* threads) {
    /* force joinable attribute, although it's default POSIX compliant */
    pthread_attr_t attr;
    if(pthread_attr_init(&attr) < 0) {
        fprintf(stdout,"error: pthread_attr_init failed\n");
        return -1;
    }
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE) < 0) {
        fprintf(stdout,"error: pthread_sttr_setdeatchstate failed\n");
        pthread_attr_destroy(&attr);
        return -1;
    }

    /* create / send threads */
    long t;
    int error=0;
    for(t=0; t<NUM_THREADS && error == 0; t++) {
        error = pthread_create(&threads[t], &attr, _test_thread_returnOne, NULL);
        if(error < 0) {
            fprintf(stdout, "error: pthread_create failed!\n");
            pthread_attr_destroy(&attr);
            return -1;
        }
    }

    /* try to join the threads, checking return value */
    if(_test_joinThreads(threads) < 0) {
        fprintf(stdout, "########## _test_joinThreads(threads) failed\n");
        pthread_attr_destroy(&attr);
        return -1;
    }

    /* success! */
    pthread_attr_destroy(&attr);
    return 0;
}

static int _test_mutex_lock(pthread_t* threads) {
    int sum=0;
    
    pthread_mutex_t mux;
    if(pthread_mutex_init(&mux, NULL) < 0) {
        fprintf(stdout, "error: pthread_mutex_init failed\n");
        return -1;
    }

    struct mux_sum ms;
    ms.sum = &sum;
    ms.mux = &mux;

    /* create / send threads */
    long t;
    int error=0;
    for(t=0; t<NUM_THREADS && error == 0; t++) {
        error = pthread_create(&threads[t], NULL, _test_thread_muxlock, (void*)&ms);
        if(error < 0) {
            fprintf(stdout, "error: pthread_create failed!\n");
            pthread_mutex_destroy(&mux);
            return -1;
        }
    }

    /* join threads, check their exit values */
    intptr_t retval=0;
    for(t=0; t<NUM_THREADS && error == 0; t++) {
        error = pthread_join(threads[t], (void**)&retval);
        if(error < 0) {
            fprintf(stdout, "error: pthread_join failed!\n");
            pthread_mutex_destroy(&mux);
            return -1;
        }
        if(retval != 0) {
            fprintf(stdout, "error: retval from pthread_join is '%ld', not 0!\n", retval);
            pthread_mutex_destroy(&mux);
            return -1;
        }
    }

    int expected = 2;
    for(t=0; t<NUM_THREADS-1; t++){
        expected = expected*expected;
    }

    if(sum != expected) {
        fprintf(stdout, "error: expected '%i', sum '%i'\n", expected, sum);
        pthread_mutex_destroy(&mux);
        return -1;
    }
    /* success! */
    pthread_mutex_destroy(&mux);
    return 0;
}

static int _test_mutex_trylock(pthread_t* threads) {
    pthread_mutex_t mux;
    if(pthread_mutex_init(&mux, NULL) < 0) {
        fprintf(stdout, "error: pthread_mutex_init failed\n");
        return -1;
    }

    /* create / send threads */
    long t;
    int error=0;
    for(t=0; t<NUM_THREADS && error == 0; t++) {
        error = pthread_create(&threads[t], NULL, _test_thread_muxtrylock, (void*)&mux);
        if(error < 0) {
            fprintf(stdout, "error: pthread_create failed!\n");
            pthread_mutex_destroy(&mux);
            return -1;
        }
    }

    /* join threads, check their exit values */
    intptr_t total=0;
    intptr_t retval=0;
    for(t=0; t<NUM_THREADS && error == 0; t++) {
        error = pthread_join(threads[t], (void**)&retval);
        if(error < 0) {
            fprintf(stdout, "error: pthread_join failed!\n");
            pthread_mutex_destroy(&mux);
            return -1;
        }
        if(retval == -1) {
            fprintf(stdout, "error: retval from pthread_join is -1!\n");
            pthread_mutex_destroy(&mux);
            return -1;
        }
        total += retval;
    }

    /* check that trylock worked by checking that some threads skipped */
    if (total >= NUM_THREADS) {
        fprintf(stdout, "error: expected < %i, total was '%ld'\n", NUM_THREADS, total);
        pthread_mutex_destroy(&mux);
        return -1;
    }

    /* success! */
    pthread_mutex_destroy(&mux);
    return 0;
}
int main(int argc, char* argv[]) {
    fprintf(stdout, "########## pthreads test starting ##########\n");

    pthread_t threads[NUM_THREADS];

    /* actually 3 tests */
    if(_test_makeJoinable(threads) < 0) {
        fprintf(stdout, "########## _test_makeJoinable(threads) failed\n");
        return -1;
    }

    if(_test_mutex_lock(threads) < 0) {
        fprintf(stdout, "########## _test_mutex_lock(threads) failed\n");
        return -1;
    }

    if(_test_mutex_trylock(threads) < 0) {
        fprintf(stdout, "########## _test_mutex_trylock(threads) failed\n");
        return -1;
    }

    fprintf(stdout, "########## pthreads test passed! ##########\n");
    return 0;
}
