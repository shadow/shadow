/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/signal.h>

#include "lib/logger/logger.h"
#include "lib/shim/shim_logger.h"
#include "lib/shim/shim_syscall.h"
#include "lib/shim/shim_tls.h"

// Handler function that receives syscalls that are stopped by the seccomp filter.
static void _shim_seccomp_handle_sigsys(int sig, siginfo_t* info, void* voidUcontext) {
    ucontext_t* ctx = (ucontext_t*)(voidUcontext);
    if (sig != SIGSYS) {
        abort();
    }
    greg_t* regs = ctx->uc_mcontext.gregs;
    const int REG_N =  REG_RAX;
    const int REG_ARG1 = REG_RDI;
    const int REG_ARG2 = REG_RSI;
    const int REG_ARG3 = REG_RDX;
    const int REG_ARG4 = REG_R10;
    const int REG_ARG5 = REG_R8;
    const int REG_ARG6 = REG_R9;

    trace("Trapped syscall %lld at %p", regs[REG_N], (void*)regs[REG_RIP]);

    // Make the syscall via the *the shim's* syscall function (which overrides
    // libc's).  It in turn will either emulate it or (if interposition is
    // disabled), make the call natively. In the latter case, the syscall
    // will be permitted to execute by the seccomp filter.
    long rv = shim_syscall(ctx, regs[REG_N], regs[REG_ARG1], regs[REG_ARG2], regs[REG_ARG3],
                           regs[REG_ARG4], regs[REG_ARG5], regs[REG_ARG6]);
    trace("Trapped syscall %lld returning %ld", ctx->uc_mcontext.gregs[REG_RAX], rv);
    ctx->uc_mcontext.gregs[REG_RAX] = rv;
}

void shim_seccomp_init() {
    // Install signal sigsys signal handler, which will receive syscalls that
    // get stopped by the seccomp filter. Shadow's emulation of signal-related
    // system calls will prevent this action from later being overridden by the
    // virtual process.
    struct sigaction old_action;
    if (sigaction(SIGSYS,
                  &(struct sigaction){
                      .sa_sigaction = _shim_seccomp_handle_sigsys,
                      // SA_NODEFER: Allow recursive signal handling, to handle a syscall
                      // being made during the handling of another. For example, we need this
                      // to properly handle the case that we end up logging from the syscall
                      // handler, and the IO syscalls themselves are trapped.
                      // SA_SIGINFO: Required because we're specifying sa_sigaction.
                      // SA_ONSTACK: Use the alternate signal handling stack, to avoid interfering
                      // with userspace thread stacks.
                      .sa_flags = SA_NODEFER | SA_SIGINFO | SA_ONSTACK,
                  },
                  &old_action) < 0) {
        panic("sigaction: %s", strerror(errno));
    }
    if (old_action.sa_handler || old_action.sa_sigaction) {
        warning("Overwrite handler for SIGSYS (%p)", old_action.sa_handler
                                                         ? (void*)old_action.sa_handler
                                                         : (void*)old_action.sa_sigaction);
    }

    // Ensure that SIGSYS isn't blocked. This code runs in the process's first
    // thread, so the resulting mask will be inherited by subsequent threads.
    // Shadow's emulation of signal-related system calls will prevent it from
    // later becoming blocked.
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGSYS);
    if (sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
        panic("sigprocmask: %s", strerror(errno));
    }

    // Setting PR_SET_NO_NEW_PRIVS allows us to install a seccomp filter without
    // CAP_SYS_ADMIN.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        panic("prctl: %s", strerror(errno));
    }

    // These symbols are defined by the linker, and give the bounds of the code
    // section `shadow_allow_syscalls`. If these become link errors, we may need to add/modify
    // a linker script to explicitly define such symbols ourselves.
    // https://stackoverflow.com/questions/4156585/how-to-get-the-length-of-a-function-in-bytes/22047976#comment83965391_22047976
    //
    // I wasn't able to find clear documentation from `ld` itself about how and
    // when these symbols are generated, but [`ld(1)`](https://www.man7.org/linux/man-pages/man1/ld.1.html)
    // does mention them in passing; e.g. the `start-stop-visibility` option controls
    // their visibility.
    extern char __start_shadow_allow_syscalls[];
    extern char __stop_shadow_allow_syscalls[];

    /* A bpf program to be loaded as a `seccomp` filter. Unfortunately the
     * documentation for how to write this is pretty sparse. There's a useful
     * example in samples/seccomp/bpf-direct.c of the Linux kernel source tree.
     * The best reference I've been able to find is a BSD man page:
     * https://www.freebsd.org/cgi/man.cgi?query=bpf&sektion=4&manpath=FreeBSD+4.7-RELEASE
     */
    struct sock_filter filter[] = {
        /* accumulator := syscall number */
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, nr)),

        /* Always allow sigreturn; otherwise we'd crash returning from our signal handler. */
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_rt_sigreturn, /*true-skip=*/0, /*false-skip=*/1),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

        /* Always allow sched_yield. Sometimes used in IPC with Shadow; emulating
         * would add unnecessary overhead, and potentially cause recursion.
         * `shadow_spin_lock` relies on this exception
         *
         * TODO: Remove this exception, as it could interfere with escaping busy-loops
         * in managed code.
         */
        BPF_JUMP(BPF_JMP + BPF_JEQ + BPF_K, SYS_sched_yield, /*true-skip=*/0, /*false-skip=*/1),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

        /* If the instruction pointer is in section `shadow_allow_syscalls`,
         * allow. This lets us define functions that are allowed to make native
         * syscalls, by placing them in that section using
         * `__attribute__((noinline, section("shadow_allow_syscalls")))` in C
         * or (untested) `#[link_section = "shadow_allow_syscalls"]` in Rust.
         */
        BPF_STMT(BPF_LD + BPF_W + BPF_ABS, offsetof(struct seccomp_data, instruction_pointer)),
        BPF_JUMP(BPF_JMP + BPF_JGT + BPF_K, (long)__stop_shadow_allow_syscalls,
                 /*true-skip=*/2, /*false-skip=*/0),
        BPF_JUMP(BPF_JMP + BPF_JGE + BPF_K, (long)__start_shadow_allow_syscalls, /*true-skip=*/0,
                 /*false-skip=*/1),
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_ALLOW),

    /* This block was intended to whitelist reads and writes to a socket
     * used to communicate with Shadow. It turns out to be unnecessary though,
     * because the functions we're using are already wrapped, and so go through
     * shim_native_syscallv, and so end up already being whitelisted above based on that.
     * (Also ended up switching back to shared-mem-based IPC instead of a socket).
     *
     * Keeping the code around for now in case we end up needing it or something similar.
     */
#if 0
        /* check_socket: Allow reads and writes to shadow socket */
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, SYS_read, 0, 2/*check_fd*/),
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, SYS_write, 0, 1/*check_fd*/),
        /* Skip to instruction pointer check */
        BPF_JUMP(BPF_JMP+BPF_JA, 3/* check_ip */, 0, 0),
        /* check_fd */
        /* accumulator := arg1 */
        BPF_STMT(BPF_LD+BPF_W+BPF_ABS, offsetof(struct seccomp_data, args[0])),
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, toShadowFd, 0, 1/* check_ip */),
        BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
#endif

        /* Trap to our syscall handler */
        BPF_STMT(BPF_RET + BPF_K, SECCOMP_RET_TRAP),
    };
    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter,
    };

    // Re SECCOMP_FILTER_FLAG_SPEC_ALLOW: Without this flag, installing a
    // seccomp filter sets the PR_SPEC_FORCE_DISABLE bit (see prctl(2)). This
    // results in a significant performance penalty. Meanwhile Shadow is
    // semi-cooperative with its virtual processes; it doesn't try to protect
    // itself or the system from malicious code. Hence, it isn't worth paying
    // this overhead.
    if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, SECCOMP_FILTER_FLAG_SPEC_ALLOW, &prog)) {
        panic("seccomp: %s", strerror(errno));
    }
}
