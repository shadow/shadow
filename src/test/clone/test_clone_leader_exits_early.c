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

#include <asm/ldt.h>

#include "test/test_common.h"
#include "test/test_glib_helpers.h"

#define CLONE_TEST_STACK_NBYTES (4*4096)

// Common flags to CLONE used throughout.
#define CLONE_FLAGS                                                                                \
    (CLONE_VM        /* Share process memory */                                                    \
     | CLONE_FS      /* Share file attributes */                                                   \
     | CLONE_FILES   /* Share open files */                                                        \
     | CLONE_SIGHAND /* Share signal dispositions */                                               \
     | CLONE_THREAD  /* Share thread-group */                                                      \
     | CLONE_SYSVSEM /* Share semaphore values */                                                  \
     | CLONE_SETTLS) /* Set thread-local-storage */

// The "empty" descriptor. We use this to create threads without TLS set up.
// See arch/x86/include/asm/desc.h and arch/x86/kernel/ldt.c in Linux source.
//
// Using this together wtih CLONE_SETTLS tells the kernel to give us an empty
// thread-local-storage descriptor. In the shadow shim's
// thread local storage, we recognize this case and fall back to an "external"
// implementation.
//
// It would be nice if we could set up a proper native TLS descriptor, but I
// don't think there's a way to do it without interfering with libc's global
// state. We might be able to do it if this entire test and the shim were
// completely free of libc dependencies.
struct user_desc LDT_EMPTY = {.read_exec_only = 1, .seg_not_present = 1};

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
                              CLONE_FLAGS | CLONE_CHILD_CLEARTID, NULL, NULL, &LDT_EMPTY, ctid);
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
                              ctid, NULL, &LDT_EMPTY, NULL);
        g_assert_cmpint(child_tid, >, 0);

        // Intentionally leak `stack`.
    }

    // Intentionally leak `ctid`.
}

int main(int argc, char** argv) {
    g_test_init(&argc, &argv, NULL);

    // This should be the only test in this test program. It exits the thread group
    // leader (this thread), so doesn't play well with other tests.
    g_test_add("/clone/clone_child_exits_after_leader", void, NULL, NULL,
               _clone_child_exits_after_leader, NULL);

    int rv = g_test_run();

    // For the `clone_child_exits_after_leader` test to be valid, we need to
    // explicitly exit *just* this thread. Returning will kill the whole
    // process.
    _exit_thread(rv);
}
