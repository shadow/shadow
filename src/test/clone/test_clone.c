/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <sched.h>
#include <signal.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>

#define CLONE_TEST_STACK_NBYTES 4096

#define CLONE_FLAGS                                                                                \
    (CLONE_VM         /* Share process memory */                                                   \
     | CLONE_FS       /* Share file attributes */                                                  \
     | CLONE_FILES    /* Share open files */                                                       \
     | CLONE_SIGHAND  /* Share signal dispositions */                                              \
     | CLONE_THREAD   /* Share thread-group */                                                     \
     | CLONE_SYSVSEM) /* Share semaphore values */

_Noreturn static void _exit_thread(int code) {
    // Exit only this thread. On some platforms returning would result in a
    // SYS_exit_group, which would kill our whole test process.
    //
    // Likewise the libc function `exit` calls the syscall `exit_group`, which exits the whole
    // process.
    //
    // We want the SYS_exit syscall, which exits just the current thread. There is no libc wrapper
    // for it.
    syscall(SYS_exit, code);
    abort();  // Unreachable.
}

static volatile int _clone_minimal_acc = 0;

// _clone_testCloneStandardFlags calls this upon cloning
static int _clone_minimal_thread(void* args) {
    ++_clone_minimal_acc;
    _exit_thread(0);
}

static void _clone_minimal() {
    // allocate some memory for the cloned thread.
    uint8_t* stack = calloc(CLONE_TEST_STACK_NBYTES, 1);

    // Heap grows up,
    // Stack grows down,
    // Head, shoulders, knees, and toes...
    uint8_t* stack_top = stack + CLONE_TEST_STACK_NBYTES;

    int child_tid = clone(_clone_minimal_thread, stack_top, CLONE_FLAGS, NULL, NULL, NULL);
    g_assert_cmpint(child_tid, >, 0);

    // The conventional way to wait for a child is futex, but we don't want this
    // test to rely on it.
    //
    // We can't use `wait` etc, because the child "thread" process's parent is
    // *this process's parent*, not this process. We might be able to work around
    // this by forking first so that we can wait in the parent of the threaded process
    // (using __WCLONE), but we don't want this test rely on fork, either.
    while (!_clone_minimal_acc) {
        usleep(1);
    }
    g_assert_cmpint(_clone_minimal_acc, ==, 1);

    free(stack);
}

static volatile int _clone_child_exits_after_leader_acc = 0;

static int _clone_child_exits_after_leader_thread(void* args) {
    ++_clone_child_exits_after_leader_acc;
    // Racy when executed natively (but test will still pass). In Shadow this
    // should deterministically ensure that this thread exits after the leader
    // thread.
    usleep(100);
    _exit_thread(0);
}

static void _clone_child_exits_after_leader() {
    // allocate some memory for the cloned thread.
    uint8_t* stack = calloc(CLONE_TEST_STACK_NBYTES, 1);

    // Heap grows up,
    // Stack grows down,
    // Head, shoulders, knees, and toes...
    uint8_t* stack_top = stack + CLONE_TEST_STACK_NBYTES;

    int child_tid =
        clone(_clone_child_exits_after_leader_thread, stack_top, CLONE_FLAGS, NULL, NULL, NULL);
    g_assert_cmpint(child_tid, >, 0);

    // The conventional way to wait for a child is futex, but we don't want this
    // test to rely on it.
    //
    // We can't use `wait` etc, because the child "thread" process's parent is
    // *this process's parent*, not this process. We might be able to work around
    // this by forking first so that we can wait in the parent of the threaded process
    // (using __WCLONE), but we don't want this test rely on fork, either.
    while (!_clone_child_exits_after_leader_acc) {
        usleep(1);
    }
    g_assert_cmpint(_clone_child_exits_after_leader_acc, ==, 1);

    free(stack);
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add("/clone/clone_minimal", void, NULL, NULL, _clone_minimal, NULL);
    // FIXME: currently hangs under shadow. The exit_group sycall is executed
    // natively from the thread leader, which does cause all threads to exit.
    // In Shadow, however, the corresponding Shadow thread remains blocked by
    // its nanosleep syscall. We probably want to add a handler for the
    // exit_group syscall to wake-up/terminate the other threads in the
    // process.
    // g_test_add("/clone/clone_child_exits_after_leader", void, NULL, NULL,
    // _clone_child_exits_after_leader, NULL);

    return g_test_run();
}
