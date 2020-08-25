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

static int _clone_test_acc = 0;

// _clone_testCloneStandardFlags calls this upon cloning
static int _clone_testStandardFlagsTarget(void* args) {
    _clone_test_acc += 1;
    return 0;
}

static void _clone_testCloneStandardFlags() {

    // stack allocate some memory for the cloned thread.
    alignas(CLONE_TEST_STACK_NBYTES) uint8_t stack[CLONE_TEST_STACK_NBYTES];

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

    flags |= CLONE_PTRACE;

    // Use SIGCHLD to tell the parent process that we've terminated.
    int child_tid =
        clone(_clone_testStandardFlagsTarget, stack_top, flags | SIGCHLD, NULL, NULL, NULL);

    printf("%d\n", child_tid);

    return;

    g_assert_cmpint(child_tid, !=, -1);

    // Wait for the only child to exit.
    int wait_pid = waitpid(-1, NULL, 0);

#if 0
    // FIXME
    sleep(1); // Running into some memory weirdness without this sleep. Weird,
              // because wait_pid should catch the issue...
#endif // 0

    g_assert_cmpint(_clone_test_acc, ==, 2);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add("/clone/clone_testCloneStandardFlags", void, NULL, NULL,
               _clone_testCloneStandardFlags, NULL);

    return g_test_run();

    return 0;
}
