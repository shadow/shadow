/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <signal.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>

#define CLONE_TEST_STACK_NBYTES 4096

static volatile int _clone_test_acc = 0;

// _clone_testCloneStandardFlags calls this upon cloning
static int _inc_clone_test_acc(void* args) {
    ++_clone_test_acc;
    return 0;
}

static void _clone_minimal() {
    // allocate some memory for the cloned thread.
    uint8_t* stack = calloc(CLONE_TEST_STACK_NBYTES, 1);

    // Heap grows up,
    // Stack grows down,
    // Head, shoulders, knees, and toes...
    uint8_t* stack_top = stack + CLONE_TEST_STACK_NBYTES;

    // Some of the standard flags used in a pthread_create() call:
    int flags = 0;
    flags |= CLONE_VM;      // Share process memory
    flags |= CLONE_FS;      // Share file attributes
    flags |= CLONE_FILES;   // Share open files
    flags |= CLONE_SIGHAND; // Share signal dispositions
    flags |= CLONE_THREAD;  // Share thread-group
    flags |= CLONE_SYSVSEM; // Share semaphore values

    int child_tid = clone(_inc_clone_test_acc, stack_top, flags, NULL, NULL, NULL);
    g_assert_cmpint(child_tid, >, 0);

    // The conventional way to wait for a child is futex, but we don't want this
    // test to rely on it.
    //
    // We can't use `wait` etc, because the child "thread" process's parent is
    // *this process's parent*, not this process. We might be able to work around
    // this by forking first so that we can wait in the parent of the threaded process
    // (using __WCLONE), but we don't want this test rely on fork, either.
    while (!_clone_test_acc) {
        usleep(1);
    }
    g_assert_cmpint(_clone_test_acc, ==, 1);

    free(stack);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add("/clone/clone_minimal", void, NULL, NULL, _clone_minimal, NULL);

    return g_test_run();
}
