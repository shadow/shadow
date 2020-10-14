/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <glib.h>

#include "support/logger/logger.h"
#include "test/test_glib_helpers.h"

#define UNAVAILABLE 0
#define AVAILABLE 1

// Spins until `c` becomes true.
static void _wait_for_condition(bool* c) {
    while (!*c) {
        // Wait a bit.
        usleep(1000);
        // Not sure this is necessary.
        __sync_synchronize();
    }
}

// Set `c` to true.
static void _set_condition(bool *c) {
    *c = true;
    __sync_synchronize();
}

// Get `c`'s value.
static bool _get_condition(bool *c) {
    __sync_synchronize();
    return *c;
}

typedef struct {
    bool child_started;
    int futex;
    bool child_finished;
} FutexWaitTestChildArg;

static void* _futex_wait_test_child(void *void_arg) {
    FutexWaitTestChildArg* arg = void_arg;
    _set_condition(&arg->child_started);
    debug("Child about to wait");
    assert_true_errno(syscall(SYS_futex, &arg->futex, FUTEX_WAIT, UNAVAILABLE, NULL, NULL, 0) == 0);
    debug("Child returned from wait");
    __sync_synchronize();
    g_assert_cmpint(arg->futex, ==, AVAILABLE);
    _set_condition(&arg->child_finished);
    debug("Child finished");
    return NULL;
}

static void _futex_wait_test() {
    FutexWaitTestChildArg arg = {
        .child_started = false, .futex = UNAVAILABLE, .child_finished = false};
    pthread_t child = {0};
    assert_nonneg_errno(pthread_create(&child, NULL, _futex_wait_test_child, &arg));

    // Wait for it to signal it's started.
    debug("Waiting for child to start");
    _wait_for_condition(&arg.child_started);

    // Try to wait until child is sleeping on the lock. Not sure if there's a reasonable way to
    // avoid a race here.
    usleep(1000);

    // Verify that it *hasn't* woken yet.
    g_assert_true(!_get_condition(&arg.child_finished));

    // Wake the child.
    debug("Waking child\n");
    g_assert_true(__sync_bool_compare_and_swap(&arg.futex, UNAVAILABLE, AVAILABLE));
    int res = syscall(SYS_futex, &arg.futex, FUTEX_WAKE, 1, NULL, NULL, 0);
    // Should have woken exactly one sleeping thread.
    g_assert_cmpint(res, ==, 1);

    // wait for it to signal that it's woken
    _wait_for_condition(&arg.child_finished);
}

static void _futex_wait_stale_test() {
    int futex = AVAILABLE;
    g_assert_cmpint(syscall(SYS_futex, &futex, FUTEX_WAIT, UNAVAILABLE, NULL, NULL, 0), ==, -1);
    assert_errno_is(EAGAIN);
}

static void _futex_wake_nobody_test() {
    int futex = AVAILABLE;
    g_assert_cmpint(syscall(SYS_futex, &futex, FUTEX_WAKE, INT_MAX), ==, 0);
}

// Note: this test roughly follows the example at the end of `man 2 futex`

#define PTR_TO_INT(p) ((int)(long)(p))
#define INT_TO_PTR(i) ((void*)(long)(i))
#define NUM_LOOPS 100
#define UNAVAILABLE 0
#define AVAILABLE 1

// The futex word used to synchronize threads
volatile int futex_word1 = UNAVAILABLE; // initial state is unavailable
volatile int futex_word2 = AVAILABLE; // initial state is available
volatile int is_child_finished = 0;
void* child_result = NULL;

// Acquire: wait for the futex pointed to by `word` to become 1, then set to 0
static int _futex_wait(volatile int* word) {
    while (1) {
        // Args are: ptr, expected old val, desired new val
        bool is_available = AVAILABLE == __sync_val_compare_and_swap(word, AVAILABLE, UNAVAILABLE);

        if (is_available) {
            break;
        } else {
            int res = syscall(SYS_futex, word, FUTEX_WAIT, UNAVAILABLE, NULL, NULL, 0);
            if (res == -1 && errno != EAGAIN) {
                char errbuf[32] = {0};
                strerror_r(errno, errbuf, 32);
                printf("FUTEX_WAIT syscall failed: error %i: %s", errno, errbuf);
                return EXIT_FAILURE;
            }
        }
    }

    return EXIT_SUCCESS;
}

// Release: if the futex pointed to by `word` is 0, set to 1 and wake blocked waiters
static int _futex_post(volatile int* word) {
    bool is_posted = UNAVAILABLE == __sync_val_compare_and_swap(word, UNAVAILABLE, AVAILABLE);

    if (is_posted) {
        int res = syscall(SYS_futex, word, FUTEX_WAKE, AVAILABLE, NULL, NULL, 0);
        if (res == -1) {
            char errbuf[32] = {0};
            strerror_r(errno, errbuf, 32);
            printf("FUTEX_WAKE syscall failed: error %i: %s", errno, errbuf);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static int _run_futex_loop(volatile int* word1, volatile int* word2, int slow) {
    int threadID = 0;
#ifdef SYS_gettid
    threadID = syscall(SYS_gettid);
#endif

    for (int j = 1; j <= NUM_LOOPS; j++) {
        // Slow down one thread to increase the chance that we'll need a FUTEX_WAIT syscall
        if (slow) {
            usleep(1000);
        }

        if (_futex_wait(word1) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }

        printf("thread %i loop %i/%i\n", threadID, j, NUM_LOOPS);

        if (_futex_post(word2) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}

static void* _run_main_thread(void* arg) {
    return INT_TO_PTR(_run_futex_loop(&futex_word1, &futex_word2, 0));
}

static void* _run_aux_thread(void* arg) {
    child_result = INT_TO_PTR(_run_futex_loop(&futex_word2, &futex_word1, 1));
    is_child_finished = 1;
    return child_result;
}

static void* _collect_child_result() {
    // The conventional way to wait for a child is futex, but we don't want this
    // test to rely on it. Instead, we busy look on an atomic flag.
    //
    // We can't use `wait` etc, because the child "thread" process's parent is
    // *this process's parent*, not this process. We might be able to work around
    // this by forking first so that we can wait in the parent of the threaded process
    // (using __WCLONE), but we don't want this test rely on fork, either.
    while (!is_child_finished) {
        usleep(1);
    }
    return child_result;
}

int run() {
    pthread_t aux_thread = {0};

    if (pthread_create(&aux_thread, NULL, _run_aux_thread, NULL) < 0) {
        printf("thread creation failed: error %i: %s\n", errno, strerror(errno));
        return EXIT_FAILURE;
    }

    void* main_result = _run_main_thread(NULL);
    void* aux_result = _collect_child_result();

    bool main_success = PTR_TO_INT(main_result) == 0;
    bool aux_success = PTR_TO_INT(aux_result) == 0;

    if (main_success && aux_success) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

int main() {
    printf("########## futex test starting ##########\n");

    _futex_wait_test();
    _futex_wait_stale_test();
    _futex_wake_nobody_test();

    if (run() == EXIT_SUCCESS) {
        printf("########## futex test passed ##########\n");
        return EXIT_SUCCESS;
    } else {
        printf("########## futex test failed ##########\n");
        return EXIT_FAILURE;
    }
}
