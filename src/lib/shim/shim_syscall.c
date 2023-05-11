#include <alloca.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_signals.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_tls.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall_numbers.h"

// Never inline, so that the seccomp filter can reliably whitelist a syscall from
// this function.
// TODO: Drop if/when we whitelist using /proc/self/maps
long __attribute__((noinline))
shim_clone(int32_t flags, void* child_stack, pid_t* ptid, pid_t* ctid, uint64_t newtls) {
    long rv = 0;
    // Make the clone syscall, and then in the child thread initialize the shim's state,
    // and then *jump* to the instruction after the original clone syscall instruction.
    //
    // Forcing the thread initialization to happen here instead of doing it
    // lazily ensures that we don't try to install the sigaltstack from
    // inside the seccomp signal handler. Doing so "works" without error,
    // but is reverted when the signal handler returns.
    //
    // Note that from the child thread's point of view, many of the general purpose
    // registers will have different values than they had in the parent thread just-before.
    // I can't find any documentation on whether the child thread is allowed to make
    // any assumptions about the state of such registers, but glibc's implementation
    // of the clone library function doesn't. If we had to, we could save and restore
    // the other registers in the same way as we are the RIP register.
    register pid_t* r10 __asm__("r10") = ctid;
    register long r8 __asm__("r8") = newtls;
    // Store clone_rip in a callee-save register, so that we can call shim_ensure_init
    // without clobbering it.
    register long r12 __asm__("r12") = (long)&shim_parent_thread_ctx;
    // Store address of shim_ensure_init in a callee-save register.
    // TODO: We ought to be able to put the literal "shim_ensure_init" in
    // the inline assembly and have the assembler resolve the address for
    // us, but on ubuntu 18.04's gcc this ends up generating a relocation it
    // can't resolve at the link step.
    register long r13 __asm__("r13") = (long)&shim_ensure_init;
    __asm__ __volatile__("syscall\n"
                         // If in the parent, done with asm.
                         "cmp $0, %%rax\n"
                         "jne shim_native_syscallv_out\n"
                         // Initialize state for this thread
                         "callq *%%r13\n"
                         // Get pointer to ucontext_t
                         "callq *%%r12\n"
                         // Load original RIP
                         "mov %c[RipOffset](%%rax), %%r12\n"
                         // Restore return value of clone
                         "movq $0, %%rax\n"
                         // Jump to original RIP.
                         "jmp *%%r12\n"
                         "shim_native_syscallv_out:\n"
                         : "=a"(rv)
                         : "a"(SYS_clone), "D"(flags), "S"(child_stack), "d"(ptid), "r"(r10),
                           "r"(r8), "r"(r12), "r"(r13),
                           [RipOffset] "i"(offsetof(ucontext_t, uc_mcontext) +
                                           offsetof(mcontext_t, gregs) + REG_RIP * sizeof(uint64_t))
                         : "rcx", "r11", "memory");
    // Wait for child to initialize itself.
    shim_newThreadFinish();
    return rv;
}

// Never inline, so that the seccomp filter can reliably whitelist a syscall from
// this function.
// TODO: Drop if/when we whitelist using /proc/self/maps
long __attribute__((noinline)) shim_native_syscallv(long n, va_list args) {
    long arg1 = va_arg(args, long);
    long arg2 = va_arg(args, long);
    long arg3 = va_arg(args, long);
    long arg4 = va_arg(args, long);
    long arg5 = va_arg(args, long);
    long arg6 = va_arg(args, long);
    long rv;

    if (n == SYS_clone) {
        int32_t flags = (int32_t)arg1;
        void* child_stack = (void*)arg2;
        pid_t* ptid = (pid_t*)arg3;
        pid_t* ctid = (pid_t*)arg4;
        uint64_t newtls = arg5;
        rv = shim_clone(flags, child_stack, ptid, ctid, newtls);
    } else {
        // r8, r9, and r10 aren't supported as register-constraints in
        // extended asm templates. We have to use [local register
        // variables](https://gcc.gnu.org/onlinedocs/gcc/Local-Register-Variables.html)
        // instead. Calling any functions in between the register assignment and the
        // asm template could clobber these registers, which is why we don't do the
        // assignment directly above.
        register long r10 __asm__("r10") = arg4;
        register long r8 __asm__("r8") = arg5;
        register long r9 __asm__("r9") = arg6;
        __asm__ __volatile__("syscall"
                             : "=a"(rv)
                             : "a"(n), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9)
                             : "rcx", "r11", "memory");
    }
    return rv;
}

// Handle to the real syscall function, initialized once at load-time for
// thread-safety.
long shim_native_syscall(long n, ...) {
    va_list args;
    va_start(args, n);
    long rv = shim_native_syscallv(n, args);
    va_end(args);
    return rv;
}

static SysCallReg _shim_emulated_syscall_event(const ucontext_t* ctx,
                                               const ShimEvent* syscall_event) {

    struct IPCData* ipc = shim_thisThreadEventIPC();

    trace("sending syscall %ld event on %p",
          shimevent_getSyscallData(syscall_event)->syscall_args.number, ipc);

    shimevent_sendEventToShadow(ipc, syscall_event);
    SysCallReg rv = {0};

    while (true) {
        trace("waiting for event on %p", ipc);
        ShimEvent res = {0};
        shimevent_recvEventFromShadow(ipc, &res);
        trace("got response of type %d on %p", shimevent_getId(&res), ipc);

        switch (shimevent_getId(&res)) {
            case SHIM_EVENT_SYSCALL_COMPLETE: {
                const ShimEventSyscallComplete* syscall_complete =
                    shimevent_getSyscallCompleteData(&res);
                // We'll ultimately return the provided result.
                SysCallReg rv = syscall_complete->retval;

                if (!shim_hostSharedMem() || !shim_processSharedMem() || !shim_threadSharedMem()) {
                    // We get here while initializing shim_threadSharedMem
                    return rv;
                }

                // Process any signals, which may have resulted from the syscall itself
                // (e.g. `kill(getpid(), signo)`), or may have been sent by another thread
                // while this one was blocked in a syscall.
                ShimShmemHostLock* host_lock = shimshmemhost_lock(shim_hostSharedMem());
                const bool allSigactionsHadSaRestart = shim_process_signals(host_lock, NULL);
                shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);

                // Check whether a blocking syscall was interrupted by a signal.
                // Note that handlers don't usually return -EINTR directly;
                // instead `syscall_handler_make_syscall` converts "blocked"
                // results to -EINTR when an unblocked signal is pending.
                if (rv.as_i64 == -EINTR) {
                    // Syscall was interrupted by a signal. Consider restarting. See signal(7).
                    const bool syscallSupportsSaRestart = syscall_complete->restartable;
                    trace("Syscall interrupted by signals. allSigactionsHadSaRestart:%d "
                          "syscallSupportsSaRestart:%d",
                          allSigactionsHadSaRestart, syscallSupportsSaRestart);
                    if (allSigactionsHadSaRestart && syscallSupportsSaRestart) {
                        shimevent_sendEventToShadow(ipc, syscall_event);
                        continue;
                    }
                }

                return rv;
            }
            case SHIM_EVENT_SYSCALL_DO_NATIVE: {
                // Make the original syscall ourselves and use the result.
                const ShimEventSyscall* syscall = shimevent_getSyscallData(syscall_event);
                const SysCallReg* regs = syscall->syscall_args.args;
                SysCallReg rv = {.as_i64 = shim_native_syscall(syscall->syscall_args.number,
                                                               regs[0].as_u64, regs[1].as_u64,
                                                               regs[2].as_u64, regs[3].as_u64,
                                                               regs[4].as_u64, regs[5].as_u64)};

                int straceFd = shimshmem_getProcessStraceFd(shim_processSharedMem());

                // shadow would have alrady logged the syscall and arguments but wouldn't have
                // logged the return value, so we can log it here
                if (straceFd >= 0) {
                    // TODO: format the time
                    uint64_t emulated_time_ms = shim_sys_get_simtime_nanos();
                    pid_t tid = shimshmem_getThreadId(shim_threadSharedMem());

                    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

                    char buf[100] = {0};
                    int len = snprintf(buf, sizeof(buf), "%018ld [tid %d] ^^^ = %ld\n",
                                       emulated_time_ms, tid, rv.as_i64);
                    len = MIN(len, sizeof(buf));

                    int written = 0;
                    while (1) {
                        int write_rv = write(straceFd, buf + written, len - written);
                        if (write_rv < 0) {
                            if (errno == -EINTR || errno == -EAGAIN) {
                                continue;
                            }
                            warning("Unable to write to strace log");
                            break;
                        }
                        written += write_rv;
                        if (written == len) {
                            break;
                        }
                    }

                    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
                }

                return rv;
            }
            case SHIM_EVENT_SYSCALL: {
                // Make the requested syscall ourselves and return the result
                // to Shadow.
                const ShimEventSyscall* syscall = shimevent_getSyscallData(&res);
                const SysCallReg* regs = syscall->syscall_args.args;
                long syscall_rv = shim_native_syscall(
                    syscall->syscall_args.number, regs[0].as_u64, regs[1].as_u64, regs[2].as_u64,
                    regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
                ShimEvent syscall_complete_event;
                shimevent_initSysCallComplete(
                    &syscall_complete_event, (SysCallReg){.as_i64 = syscall_rv}, false);
                shimevent_sendEventToShadow(ipc, &syscall_complete_event);
                break;
            }
            case SHIM_EVENT_ADD_THREAD_REQ: {
                const ShimEventAddThreadReq* add_thread_req = shimevent_getAddThreadReqData(&res);
                shim_newThreadStart(&add_thread_req->ipc_block, ctx);
                ShimEvent res;
                shimevent_initAddThreadParentRes(&res);
                shimevent_sendEventToShadow(ipc, &res);
                break;
            }
            default: {
                panic("Got unexpected event %d", shimevent_getId(&res));
                abort();
            }
        }
    }
}

long shim_emulated_syscallv(const ucontext_t* ctx, long n, va_list args) {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    if (n == SYS_exit) {
        // This thread is exiting. Arrange for its signal stack to be freed.
        shim_freeSignalStack();
    }

    SysCallArgs ev_args;
    ev_args.number = n;
    for (int i = 0; i < 6; ++i) {
        ev_args.args[i].as_u64 = va_arg(args, uint64_t);
    }

    ShimEvent e;
    shimevent_initSyscall(&e, &ev_args);

    SysCallReg retval = _shim_emulated_syscall_event(ctx, &e);

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);

    return retval.as_i64;
}

long shim_emulated_syscall(const ucontext_t* ctx, long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_emulated_syscallv(ctx, n, args);
    va_end(args);
    return rv;
}

long shim_syscallv(const ucontext_t* ctx, long n, va_list args) {
    shim_ensure_init();

    long rv;

    if (shim_interpositionEnabled() && shim_sys_handle_syscall_locally(n, &rv, args)) {
        // No inter-process syscall needed, we handled it on the shim side! :)
        trace("Handled syscall %ld from the shim; we avoided inter-process overhead.", n);
        // rv was already set
    } else if ((shim_interpositionEnabled() || syscall_num_is_shadow(n)) &&
               shim_thisThreadEventIPC()) {
        // The syscall is made using the shmem IPC channel.
        trace("Making syscall %ld indirectly; we ask shadow to handle it using the shmem IPC "
              "channel.",
              n);
        rv = shim_emulated_syscallv(ctx, n, args);
    } else {
        // The syscall is made directly; ptrace or seccomp will get the syscall signal.
        trace("Making syscall %ld directly; we expect ptrace or seccomp will interpose it, or it "
              "will be handled natively by the kernel.",
              n);
        rv = shim_native_syscallv(n, args);
    }

    return rv;
}

long shim_syscall(const ucontext_t* ctx, long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_syscallv(ctx, n, args);
    va_end(args);
    return rv;
}
