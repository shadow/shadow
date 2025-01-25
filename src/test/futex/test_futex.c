/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <glib.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "test/test_common.h"
#include "test/test_glib_helpers.h"

#define UNAVAILABLE 0
#define AVAILABLE 1

// Spins until `c` becomes true.
static void _wait_for_condition(atomic_bool* c) {
    // Prevent reads from being done
    while (!atomic_load(c)) {
        // Wait a bit.
        usleep(1);
    }
}

typedef struct {
    atomic_int futex;
    atomic_bool child_started;
    atomic_bool child_finished;
} FutexWaitTestChildArg;

static void* _futex_wait_test_child(void* void_arg) {
    FutexWaitTestChildArg* arg = void_arg;
    atomic_store(&arg->child_started, true);
    do {
        trace("Child about to wait");
        long rv = syscall(SYS_futex, &arg->futex, FUTEX_WAIT, UNAVAILABLE, NULL, NULL, 0);
        if (rv != 0) {
            // Failed to wait because futex is already available.
            assert_errno_is(EAGAIN);
            g_assert_cmpint(atomic_load(&arg->futex), ==, AVAILABLE);
        }
    } while (atomic_load(&arg->futex) != AVAILABLE);
    trace("Child returned from wait");
    atomic_store(&arg->child_finished, true);
    trace("Child finished");
    return NULL;
}

static void _futex_wait_test() {
    FutexWaitTestChildArg arg = {
        .child_started = false, .futex = UNAVAILABLE, .child_finished = false};
    pthread_t child = {0};
    assert_nonneg_errno(pthread_create(&child, NULL, _futex_wait_test_child, &arg));

    // Wait for it to signal it's started.
    trace("Waiting for child to start");
    _wait_for_condition(&arg.child_started);

    // Verify that it *hasn't* woken yet.
    g_assert_true(!atomic_load(&arg.child_finished));

    // Wake the child. There's no way to guarantee that the child is already asleep
    // on the mutex, so we need to loop.
    long woken = 0;
    while (1) {
        trace("Waking child\n");
        woken = syscall(SYS_futex, &arg.futex, FUTEX_WAKE, 1, NULL, NULL, 0);
        assert_nonneg_errno(woken);
        if (woken == 1) {
            trace("Woke 1 child");
            break;
        }
        g_assert_cmpint(woken, ==, 0);
        trace("No children woken; sleeping a bit and trying again");
        usleep(1);
    }

    // Flip the flag to let the child finish executing.
    g_assert_cmpint(atomic_exchange(&arg.futex, AVAILABLE), ==, UNAVAILABLE);

    // The child may or may not have gone asleep since the previous wake-up. Wake it up.
    woken = syscall(SYS_futex, &arg.futex, FUTEX_WAKE, 1, NULL, NULL, 0);
    assert_nonneg_errno(woken);
    g_assert_cmpint(woken, <=, 1);

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

double timespec_to_double(const struct timespec* t) {
    return (double)t->tv_sec + (double)t->tv_nsec / 1000000000.0;
}

static void _futex_wait_timeout_test() {

    struct timespec t0;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }

    // FUTEX_WAIT has a relative timeout.
    int futex = 0;
    struct timespec relative_timeout = {.tv_sec = 1, .tv_nsec = 0};
    long rv = syscall(SYS_futex, &futex, FUTEX_WAIT, futex, &relative_timeout);
    g_assert_cmpint(rv, ==, -1);
    assert_errno_is(ETIMEDOUT);
    struct timespec t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }
    double delta = timespec_to_double(&t1) - timespec_to_double(&t0) - 1.0;
    g_assert_cmpfloat(delta, <=, 0.1);
    g_assert_cmpfloat(delta, >=, -0.1);
}

static void _futex_wait_bitset_timeout_test() {
    // FUTEX_WAIT_BITSET has an absolute timeout.
    struct timespec t0;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }
    struct timespec timeout = {.tv_sec = t0.tv_sec + 1, .tv_nsec = t0.tv_nsec};
    int futex = 0;
    long rv = syscall(
        SYS_futex, &futex, FUTEX_WAIT_BITSET, futex, &timeout, NULL, FUTEX_BITSET_MATCH_ANY);
    g_assert_cmpint(rv, ==, -1);
    assert_errno_is(ETIMEDOUT);
    struct timespec t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }
    double delta = timespec_to_double(&t1) - timespec_to_double(&timeout);
    g_assert_cmpfloat(delta, <=, .1);
    g_assert_cmpfloat(delta, >=, -.1);
}

static void _futex_wait_bitset_timeout_expired_test() {
    // FUTEX_WAIT_BITSET has an absolute timeout.
    struct timespec t0;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }
    struct timespec timeout = {.tv_sec = 0, .tv_nsec = 0};
    int futex = 0;
    long rv = syscall(
        SYS_futex, &futex, FUTEX_WAIT_BITSET, futex, &timeout, NULL, FUTEX_BITSET_MATCH_ANY);
    g_assert_cmpint(rv, ==, -1);
    assert_errno_is(ETIMEDOUT);
    struct timespec t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }
    double delta = timespec_to_double(&t1) - timespec_to_double(&t0);
    g_assert_cmpfloat(delta, <=, .1);
    g_assert_cmpfloat(delta, >=, -.1);
}

void nop_signal_handler(int signo) {}

static void _futex_wait_intr_test() {
    assert_nonneg_errno(sigaction(SIGALRM,
                                  &(struct sigaction){
                                      .sa_handler = nop_signal_handler,
                                  },
                                  NULL));

    int intr_s = 1;
    assert_nonneg_errno(
        setitimer(ITIMER_REAL, &(struct itimerval){.it_value = {.tv_sec = intr_s}}, NULL));

    struct timespec t0;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }
    int futex = 0;
    long rv = syscall(SYS_futex, &futex, FUTEX_WAIT, futex, /*timeout*/ NULL);
    g_assert_cmpint(rv, ==, -1);
    assert_errno_is(EINTR);
    struct timespec t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0) {
        panic("clock_gettime: %s", strerror(errno));
    }
    double delta = timespec_to_double(&t1) - timespec_to_double(&t0) - intr_s;
    g_assert_cmpfloat(delta, <=, 0.1);
    g_assert_cmpfloat(delta, >=, -0.1);
}

typedef struct {
    atomic_bool child_started;
    atomic_bool child_finished;
    int id;
    atomic_int* futex;
} FutexWaitBitsetTestChildArg;

static void* _futex_wait_bitset_test_child(void* void_arg) {
    FutexWaitBitsetTestChildArg* arg = void_arg;
    atomic_store(&arg->child_started, true);
    do {
        trace("Child %d about to wait", arg->id);
        long rv = syscall(
            SYS_futex, arg->futex, FUTEX_WAIT_BITSET, UNAVAILABLE, NULL, NULL, 1 << arg->id);
        if (rv != 0) {
            g_assert_cmpint(rv, ==, -1);
            assert_errno_is(EAGAIN);
            g_assert_cmpint(atomic_load(arg->futex), ==, AVAILABLE);
        }
        trace("Child %d returned from wait", arg->id);
    } while (atomic_load(arg->futex) != AVAILABLE);
    trace("Child %d done waiting", arg->id);
    atomic_store(&arg->child_finished, true);
    trace("Child finished");
    return NULL;
}

static void _futex_wait_bitset_test() {
    FutexWaitBitsetTestChildArg arg[5];
    atomic_int futex = UNAVAILABLE;

    // Get all 5 children waiting.
    for (int i = 0; i < 5; ++i) {
        arg[i] = (FutexWaitBitsetTestChildArg){
            .child_started = false,
            .child_finished = false,
            .id = i,
            .futex = &futex,
        };

        pthread_t child = {0};
        assert_nonneg_errno(pthread_create(&child, NULL, _futex_wait_bitset_test_child, &arg[i]));
        trace("Waiting for child %d to start", i);
        _wait_for_condition(&arg[i].child_started);
    }

    // Wait a bit until they're (hopefully) all blocked on the futex.
    usleep(10000);

    // Wake only #2. There's no way to guarantee that its already asleep on the
    // mutex, so we need to loop.
    long woken = 0;
    while (1) {
        trace("Waking child\n");
        woken = syscall(SYS_futex, &futex, FUTEX_WAKE_BITSET, INT_MAX, NULL, NULL, 1 << 2);
        assert_nonneg_errno(woken);
        if (woken == 1) {
            trace("Woke 1 child");
            break;
        }
        g_assert_cmpint(woken, ==, 0);
        trace("No children woken; sleeping a bit and trying again");
        usleep(1);
    }

    // Release the futex.
    atomic_store(&futex, AVAILABLE);

    // Ensure #2 is now awake.
    woken = syscall(SYS_futex, &futex, FUTEX_WAKE_BITSET, INT_MAX, NULL, NULL, 1 << 2);
    assert_nonneg_errno(woken);
    g_assert_cmpint(woken, <=, 1);

    // Wait for #2 to signal that it's done.
    _wait_for_condition(&arg[2].child_finished);

    // The other children should still be sleeping.
    // FIXME: There is a race condition here since the thread might not have
    // gone to sleep before the futex was released.
    g_assert_false(arg[0].child_finished);
    g_assert_false(arg[1].child_finished);
    g_assert_false(arg[3].child_finished);
    g_assert_false(arg[4].child_finished);

    // Wake #1, #2, and #3. #2 should be a no-op, and we can't guarantee that #1
    // and #3 were asleep in the first place.
    woken = syscall(
        SYS_futex, &futex, FUTEX_WAKE_BITSET, INT_MAX, NULL, NULL, 1 << 1 | 1 << 2 | 1 << 3);
    assert_nonneg_errno(woken);
    g_assert_cmpint(woken, <=, 2);

    // Wait for 1 and 3 to finish.
    _wait_for_condition(&arg[1].child_finished);
    _wait_for_condition(&arg[3].child_finished);

    // Ensure 0 and 4 are awake and wait for them to finish. Exercise including
    // bits we didn't actually use.
    woken = syscall(SYS_futex, &futex, FUTEX_WAKE_BITSET, INT_MAX, NULL, NULL, 0xffffffff);
    assert_nonneg_errno(woken);
    g_assert_cmpint(woken, <=, 2);

    _wait_for_condition(&arg[0].child_finished);
    _wait_for_condition(&arg[4].child_finished);
}

// Note: this test roughly follows the example at the end of `man 2 futex`

#define PTR_TO_INT(p) ((int)(long)(p))
#define INT_TO_PTR(i) ((void*)(long)(i))
#define NUM_LOOPS 100
#define UNAVAILABLE 0
#define AVAILABLE 1

// The futex word used to synchronize threads
atomic_int futex_word1 = UNAVAILABLE; // initial state is unavailable
atomic_int futex_word2 = AVAILABLE;   // initial state is available
int is_child_finished = 0;
void* child_result = NULL;

// Acquire: wait for the futex pointed to by `word` to become 1, then set to 0
static int _futex_wait(atomic_int* word) {
    while (1) {
        int val = AVAILABLE;
        if (atomic_compare_exchange_strong(word, &val, UNAVAILABLE)) {
            break;
        }
        long res = syscall(SYS_futex, word, FUTEX_WAIT, val, NULL, NULL, 0);
        if (res != 0) {
            g_assert_cmpint(res,==,-1);
            assert_errno_is(EAGAIN);
        }
    }

    return EXIT_SUCCESS;
}

// Release: if the futex pointed to by `word` is 0, set to 1 and wake blocked waiters
static int _futex_post(atomic_int* word) {
    bool prev_val = atomic_exchange(word, AVAILABLE);

    if (prev_val == UNAVAILABLE) {
        long res = syscall(SYS_futex, word, FUTEX_WAKE, AVAILABLE, NULL, NULL, 0);
        assert_nonneg_errno(res);
    }

    return EXIT_SUCCESS;
}

static int _run_futex_loop(atomic_int* word1, atomic_int* word2, int slow) {
    pid_t threadID = 0;
#ifdef SYS_gettid
    threadID = (pid_t)syscall(SYS_gettid);
#endif

    for (int j = 1; j <= NUM_LOOPS; j++) {
        // Slow down one thread to increase the chance that we'll need a FUTEX_WAIT syscall
        if (slow) {
            usleep(1000);
        }

        if (_futex_wait(word1) != EXIT_SUCCESS) {
            return EXIT_FAILURE;
        }

        trace("thread %i loop %i/%i\n", threadID, j, NUM_LOOPS);

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

static void _futex_stress_test() {
    pthread_t aux_thread = {0};

    assert_nonneg_errno(pthread_create(&aux_thread, NULL, _run_aux_thread, NULL));

    void* main_result = _run_main_thread(NULL);
    void* aux_result = _collect_child_result();

    g_assert_cmpint(PTR_TO_INT(main_result), ==, 0);
    g_assert_cmpint(PTR_TO_INT(aux_result), ==, 0);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    g_test_add_func("/futex/wait", _futex_wait_test);
    g_test_add_func("/futex/wait_intr", _futex_wait_intr_test);
    g_test_add_func("/futex/wait_stale", _futex_wait_stale_test);
    g_test_add_func("/futex/wake_nobody", _futex_wake_nobody_test);
    g_test_add_func("/futex/wake_stress", _futex_stress_test);
    g_test_add_func("/futex/wait_timeout", _futex_wait_timeout_test);
    g_test_add_func("/futex/wait_bitset_timeout", _futex_wait_bitset_timeout_test);
    g_test_add_func("/futex/wait_bitset_timeout_expired", _futex_wait_bitset_timeout_expired_test);

    if (!running_in_shadow()) {
        // TODO: implement FUTEX_WAKE_BITSET in Shadow.
        g_test_add_func("/futex/wait_bitset", _futex_wait_bitset_test);
    }

    return g_test_run();
}
