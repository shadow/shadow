#include <alloca.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/syscall.h>

#include "lib/logger/logger.h"
#include "lib/shim/ipc.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_event.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_shmem.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_tls.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall_numbers.h"
#include "main/shmem/shmem_allocator.h"

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

    // When interposing a clone syscall, we can't return in the new child thread.
    // Instead we *jump* to just after the original syscall instruction, using
    // the RIP saved in our SIGSYS signal handler.
    //
    // TODO: it'd be cleaner for this to be a separate, dedicated, function.
    // However right now the actual clone syscall instruction *must* be executed
    // from this function to pass the seccomp filter.
    void* clone_rip = NULL;
    if (n == SYS_clone && (clone_rip = shim_seccomp_take_clone_rip()) != NULL) {
        // Make the clone syscall, and then in the child thread immediately jump
        // to the instruction after the original clone syscall instruction.
        //
        // Note that from the child thread's point of view, many of the general purpose
        // registers will have different values than they had in the parent thread just-before.
        // I can't find any documentation on whether the child thread is allowed to make
        // any assumptions about the state of such registers, but glibc's implementation
        // of the clone library function doesn't. If we had to, we could save and restore
        // the other registers in the same way as we are the RIP register.
        register long r10 __asm__("r10") = arg4;
        register long r8 __asm__("r8") = arg5;
        register long r9 __asm__("r9") = (long)clone_rip;
        __asm__ __volatile__("syscall\n"
                             "cmp $0, %%rax\n"
                             "jne shim_native_syscallv_out\n"
                             "jmp *%%r9\n"
                             "shim_native_syscallv_out:\n"
                             : "=a"(rv)
                             : "a"(n), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8),
                               "r"(r9), [CLONE_RIP] "rm"(&clone_rip)
                             : "rcx", "r11", "memory");
        // Wait for child to initialize itself.
        shim_newThreadFinish();
        return rv;
    }

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

static void _shim_process_signals(ShimShmemHostLock* host_lock) {
    int signo;
    siginfo_t siginfo;
    while ((signo = shimshmem_takePendingUnblockedSignal(
                host_lock, shim_processSharedMem(), shim_threadSharedMem(), &siginfo)) != 0) {
        shd_kernel_sigset_t blocked_signals =
            shimshmem_getBlockedSignals(host_lock, shim_threadSharedMem());

        struct shd_kernel_sigaction action =
            shimshmem_getSignalAction(host_lock, shim_processSharedMem(), signo);

        if (action.ksa_handler == SIG_IGN) {
            continue;
        }

        if (action.ksa_handler == SIG_DFL) {
            switch (shd_defaultAction(signo)) {
                case SHD_DEFAULT_ACTION_IGN:
                    // Ignore
                    continue;
                case SHD_DEFAULT_ACTION_CORE:
                case SHD_DEFAULT_ACTION_TERM: {
                    // Deliver natively to terminate/drop core.
                    if (sigaction(signo, &(struct sigaction){.sa_handler = SIG_DFL}, NULL) != 0) {
                        panic("sigaction: %s", strerror(errno));
                    }
                    shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
                    raise(signo);
                    panic("Unreachable");
                }
                case SHD_DEFAULT_ACTION_STOP: panic("Stop via signal unimplemented.");
                case SHD_DEFAULT_ACTION_CONT: panic("Continue via signal unimplemented.");
            };
            panic("Unreachable");
        }

        trace("Handling signo %d", signo);

        shd_kernel_sigset_t handler_mask = shd_sigorset(&blocked_signals, &action.ksa_mask);
        if (!(action.ksa_flags & SA_NODEFER)) {
            // Block another instance of the same signal.
            shd_sigaddset(&handler_mask, signo);
        }
        if (action.ksa_flags & SA_RESETHAND) {
            shimshmem_setSignalAction(host_lock, shim_processSharedMem(), signo,
                                      &(struct shd_kernel_sigaction){.ksa_handler = SIG_DFL});
        }
        if (action.ksa_flags & SA_ONSTACK) {
            error("SA_ONSTACK unimplemented; ignoring.");
        }
        if (action.ksa_flags & SA_RESTART) {
            error("SA_RESTART unimplemented; ignoring.");
        }

        // Call signal handler with host lock released, and native syscalls disabled.
        shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
        bool oldAllowNativeSyscalls = shim_swapAllowNativeSyscalls(false);
        if (action.ksa_flags & SA_SIGINFO) {
            // ucontext is unimplemented for now. Most signal handlers don't use
            // it, and passing NULL and getting a loud crash if they try to use
            // it is probably preferable over passing in a subtly-wrong fake.
            //
            // In cases where we intercepted the original syscall via seccomp,
            // it might be reasonable to pass the ucontext_t that *our* signal
            // handler received. Otherwise it's much murkier how such a context
            // ought to be constructed.
            ucontext_t* ucontext = NULL;
            action.ksa_sigaction(signo, &siginfo, ucontext);
        } else {
            action.ksa_handler(signo);
        }

        // Reacquire host lock and re-disable native syscalls.
        shim_swapAllowNativeSyscalls(oldAllowNativeSyscalls);
        host_lock = shimshmemhost_lock(shim_hostSharedMem());

        // Restore mask
        shimshmem_setBlockedSignals(host_lock, shim_threadSharedMem(), blocked_signals);
    }
}

static SysCallReg _shim_emulated_syscall_event(const ShimEvent* syscall_event) {

    struct IPCData* ipc = shim_thisThreadEventIPC();

    trace("sending syscall %ld event on %p", syscall_event->event_data.syscall.syscall_args.number,
          ipc);

    shimevent_sendEventToShadow(ipc, syscall_event);
    SysCallReg rv = {0};

    // By default we assume Shadow will return quickly, and so should spin
    // rather than letting the OS block this thread.
    bool spin = true;
    while (true) {
        trace("waiting for event on %p", ipc);
        ShimEvent res = {0};
        shimevent_recvEventFromShadow(ipc, &res, spin);
        trace("got response of type %d on %p", res.event_id, ipc);

        // Reset spin-flag to true. (May have been set to false by a SHD_SHIM_EVENT_BLOCK in the
        // previous iteration)
        spin = true;
        switch (res.event_id) {
            case SHD_SHIM_EVENT_BLOCK: {
                // Loop again, this time relinquishing the CPU while waiting for the next message.
                spin = false;
                // Ack the message.
                shimevent_sendEventToShadow(ipc, &res);
                break;
            }
            case SHD_SHIM_EVENT_SYSCALL_COMPLETE: {
                // We'll ultimately return the provided result.
                SysCallReg rv = res.event_data.syscall_complete.retval;

                if (!shim_hostSharedMem() || !shim_processSharedMem() || !shim_threadSharedMem()) {
                    // We get here while initializing shim_threadSharedMem
                    return rv;
                }

                // Process any signals, which may have resulted from the syscall itself
                // (e.g. `kill(getpid(), signo)`), or may have been sent by another thread
                // while this one was blocked in a syscall.
                ShimShmemHostLock* host_lock = shimshmemhost_lock(shim_hostSharedMem());
                _shim_process_signals(host_lock);
                shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);

                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL_DO_NATIVE: {
                // Make the original syscall ourselves and use the result.
                SysCallReg rv = res.event_data.syscall_complete.retval;
                const SysCallReg* regs = syscall_event->event_data.syscall.syscall_args.args;
                rv.as_i64 = shim_native_syscall(
                    syscall_event->event_data.syscall.syscall_args.number, regs[0].as_u64,
                    regs[1].as_u64, regs[2].as_u64, regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
                return rv;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                // Make the requested syscall ourselves and return the result
                // to Shadow.
                const SysCallReg* regs = res.event_data.syscall.syscall_args.args;
                long syscall_rv = shim_native_syscall(
                    res.event_data.syscall.syscall_args.number, regs[0].as_u64, regs[1].as_u64,
                    regs[2].as_u64, regs[3].as_u64, regs[4].as_u64, regs[5].as_u64);
                ShimEvent syscall_complete_event = {
                    .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                    .event_data.syscall_complete.retval.as_i64 = syscall_rv,
                };
                shimevent_sendEventToShadow(ipc, &syscall_complete_event);
                break;
            }
            case SHD_SHIM_EVENT_CLONE_REQ:
                shim_shmemHandleClone(&res);
                shim_shmemNotifyComplete(ipc);
                break;
            case SHD_SHIM_EVENT_CLONE_STRING_REQ:
                shim_shmemHandleCloneString(&res);
                shim_shmemNotifyComplete(ipc);
                break;
            case SHD_SHIM_EVENT_WRITE_REQ:
                shim_shmemHandleWrite(&res);
                shim_shmemNotifyComplete(ipc);
                break;
            case SHD_SHIM_EVENT_ADD_THREAD_REQ: {
                shim_newThreadStart(&res.event_data.add_thread_req.ipc_block);
                shimevent_sendEventToShadow(
                    ipc, &(ShimEvent){
                             .event_id = SHD_SHIM_EVENT_ADD_THREAD_PARENT_RES,
                         });
                break;
            }
            default: {
                panic("Got unexpected event %d", res.event_id);
                abort();
            }
        }
    }
}

long shim_emulated_syscallv(long n, va_list args) {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);

    ShimEvent e = {
        .event_id = SHD_SHIM_EVENT_SYSCALL,
        .event_data.syscall.syscall_args.number = n,
    };
    SysCallReg* regs = e.event_data.syscall.syscall_args.args;
    for (int i = 0; i < 6; ++i) {
        regs[i].as_u64 = va_arg(args, uint64_t);
    }

    SysCallReg retval = _shim_emulated_syscall_event(&e);

    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);

    return retval.as_i64;
}

long shim_emulated_syscall(long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_emulated_syscallv(n, args);
    va_end(args);
    return rv;
}

long shim_syscallv(long n, va_list args) {
    shim_ensure_init();

    long rv;

    if (shim_interpositionEnabled() && shim_use_syscall_handler() &&
        shim_sys_handle_syscall_locally(n, &rv, args)) {
        // No inter-process syscall needed, we handled it on the shim side! :)
        trace("Handled syscall %ld from the shim; we avoided inter-process overhead.", n);
        // rv was already set
    } else if ((shim_interpositionEnabled() || syscall_num_is_shadow(n)) && shim_thisThreadEventIPC()) {
        // The syscall is made using the shmem IPC channel.
        trace("Making syscall %ld indirectly; we ask shadow to handle it using the shmem IPC "
              "channel.",
              n);
        rv = shim_emulated_syscallv(n, args);
    } else {
        // The syscall is made directly; ptrace or seccomp will get the syscall signal.
        trace("Making syscall %ld directly; we expect ptrace or seccomp will interpose it, or it "
              "will be handled natively by the kernel.",
              n);
        rv = shim_native_syscallv(n, args);
    }

    return rv;
}

long shim_syscall(long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_syscallv(n, args);
    va_end(args);
    return rv;
}
