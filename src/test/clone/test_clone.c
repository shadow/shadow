/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <linux/futex.h>
#include <sched.h>
#include <signal.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test/test_common.h"
#include "test/test_glib_helpers.h"

#define CLONE_TEST_STACK_NBYTES (4*4096)

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

static int _clone_minimal_done = 0;

// _clone_testCloneStandardFlags calls this upon cloning
static int _clone_minimal_thread(void* args) {
    __atomic_store_n(&_clone_minimal_done, 1, __ATOMIC_RELEASE);
    _exit_thread(0);
}

static void _make_stack(void** top, void** bottom) {
    *bottom = mmap(NULL, CLONE_TEST_STACK_NBYTES, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    assert_true_errno(*bottom != MAP_FAILED);
    *top = *bottom + CLONE_TEST_STACK_NBYTES;

    // Set up a guard page. This isn't strictly necessary, but in the case that
    // this test somehow ends up overflowing the stack, will result in a more
    // consistent and easier to debug failure, since accessing this page will
    // always trigger a SEGV.
    //
    // e.g. without this, if the stack happened to be allocated adjacent to some
    // other accessible memory, then overflowing the stack could silently
    // corrupt that memory.
    assert_nonneg_errno(mprotect(*bottom, 4096, PROT_NONE));
}

static void _clone_minimal() {
    void *stack_top, *stack_bottom;
    _make_stack(&stack_top, &stack_bottom);
    int child_tid = clone(_clone_minimal_thread, stack_top, CLONE_FLAGS, NULL, NULL, NULL);
    g_assert_cmpint(child_tid, >, 0);

    // The conventional way to wait for a child is futex, but we don't want this
    // test to rely on it.
    //
    // We can't use `wait` etc, because the child "thread" process's parent is
    // *this process's parent*, not this process. We might be able to work around
    // this by forking first so that we can wait in the parent of the threaded process
    // (using __WCLONE), but we don't want this test rely on fork, either.
    while (!__atomic_load_n(&_clone_minimal_done, __ATOMIC_ACQUIRE)) {
        usleep(1);
    }
    g_assert_cmpint(_clone_minimal_done, ==, 1);

    // Intentionally leak `stack`. In this test we can't reliably know when the
    // child thread is done with it.
    // munmap(stack_bottom, CLONE_TEST_STACK_NBYTES);
}

// _clone_testCloneTids calls this upon cloning
static int _testCloneClearTidThread(void* args) {
    // Try to give parent a chance to sleep on the tid futex.
    usleep(1000);

    _exit_thread(0);
}

static void _testCloneClearTid() {
    void *stack_top, *stack_bottom;
    _make_stack(&stack_top, &stack_bottom);

    // Putting this on the stack ends up somehow tripping up gcc's
    // stack-smashing detection, so we put it on the heap instead.
    pid_t* ctid = malloc(sizeof(*ctid));
    *ctid = -1;

    pid_t tid = clone(_testCloneClearTidThread, stack_top, CLONE_FLAGS | CLONE_CHILD_CLEARTID, NULL,
                      NULL, NULL, ctid);
    assert_nonneg_errno(tid);

    long rv;
    while ((rv = syscall(SYS_futex, ctid, FUTEX_WAIT, -1, NULL, NULL, 0)) == 0 && *ctid == -1) {
        // Spurious wakeup. Try again.
        g_assert(!running_in_shadow());
    }
    if (rv == 0) {
        // Normal wakeup.
        g_assert_cmpint(*ctid, ==, 0);
    } else {
        // Child exited and set ctid before we went to sleep on the futex.
        g_assert_cmpint(rv, ==, -1);
        assert_errno_is(EAGAIN);
        g_assert_cmpint(*ctid, ==, 0);
        g_assert(!running_in_shadow());
    }

    // Because we used CLONE_CHILD_CLEARTID to be notified of the child thread
    // exit, we can safely deallocate it's stack.
    munmap(stack_bottom, CLONE_TEST_STACK_NBYTES);
}

static int _clone_child_exits_after_leader_waitee_thread(void* args) {
    // Racy when executed natively (but test will still pass). In Shadow this
    // should deterministically ensure that this thread exits after the leader
    // thread.
    usleep(100);
    _exit_thread(0);
}

static int _clone_child_exits_after_leader_waiter_thread(void* voidCtid) {
    pid_t* ctid = voidCtid;

    // Wait for the specified child to exit, using a loop to avoid relying on futex.
    while (__atomic_load_n(ctid, __ATOMIC_ACQUIRE) != 0) {
        usleep(1);
    }
    _exit_thread(0);
}

static void _clone_child_exits_after_leader() {
    pid_t* ctid = malloc(sizeof(*ctid));
    *ctid = -1;

    // Create "waitee" thread.
    {
        void *stack_top, *stack_bottom;
        _make_stack(&stack_top, &stack_bottom);

        int child_tid = clone(_clone_child_exits_after_leader_waitee_thread, stack_top,
                              CLONE_FLAGS | CLONE_CHILD_CLEARTID, NULL, NULL, NULL, ctid);
        g_assert_cmpint(child_tid, >, 0);

        // Intentionally leak `stack`.
    }

    // Create "waiter" thread. This thread waits for the "waitee" thread to
    // exit, and then exits itself. This is meant to test that Shadow still
    // correctly clears the `ctid` when the waitee thread exits. In particular
    // this is a regression test for using the pid of a dead task (the thread
    // leader) for process_vm_writev.
    {
        void *stack_top, *stack_bottom;
        _make_stack(&stack_top, &stack_bottom);

        int child_tid = clone(_clone_child_exits_after_leader_waiter_thread, stack_top, CLONE_FLAGS,
                              ctid, NULL, NULL, NULL);
        g_assert_cmpint(child_tid, >, 0);

        // Intentionally leak `stack`.
    }

    // Intentionally leak `ctid`.
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);

    g_test_add("/clone/clone_minimal", void, NULL, NULL, _clone_minimal, NULL);
    g_test_add("/clone/test_clone_clear_tid", void, NULL, NULL, _testCloneClearTid, NULL);

    // This test should be last; otherwise the thread group leader (this
    // thread) may exit before the clone-child under test.
    g_test_add("/clone/clone_child_exits_after_leader", void, NULL, NULL,
               _clone_child_exits_after_leader, NULL);

    int rv = g_test_run();

    // For the `clone_child_exits_after_leader` test to be valid, we need to
    // explicitly exit *just* this thread. Returning will kill the whole
    // process.
    _exit_thread(rv);
}
