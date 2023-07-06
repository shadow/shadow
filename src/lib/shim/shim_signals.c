#include "lib/shim/shim_signals.h"

#include "lib/linux-api/linux-api.h"
#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_api.h"

#include <errno.h>
#include <string.h>
#include <ucontext.h>

static _Noreturn void _die_with_fatal_signal(int signo) {
    shim_swapAllowNativeSyscalls(true);
    // Deliver natively to terminate/drop core.
    if (signo == SIGKILL) {
        // No need to restore default action, and trying to do so would fail.
    } else if (sigaction(signo, &(struct sigaction){.sa_handler = SIG_DFL}, NULL) != 0) {
        panic("sigaction: %s", strerror(errno));
    }
    raise(signo);
    panic("Unreachable");
}

void shim_handle_hardware_error_signal(int signo, siginfo_t* info, void* void_ucontext) {
    bool oldNativeSyscallFlag = shim_swapAllowNativeSyscalls(true);
    if (oldNativeSyscallFlag) {
        // Error was raised from shim code.
        _die_with_fatal_signal(signo);
        panic("Unreachable");
    }
    // Otherwise the error was raised from managed code, and could potentially
    // be handled by a signal handler that it installed.

    ShimShmemHostLock* host_lock = shimshmemhost_lock(shim_hostSharedMem());

    linux_sigset_t pending_signals =
        shimshmem_getThreadPendingSignals(host_lock, shim_threadSharedMem());
    if (linux_sigismember(&pending_signals, signo)) {
        warning("Received signal %d when it was already pending", signo);
    } else {
        linux_sigaddset(&pending_signals, signo);
        shimshmem_setThreadPendingSignals(host_lock, shim_threadSharedMem(), pending_signals);
        // So far we've gotten away with assuming that the libc and kernel
        // siginfo_t's are the same.
        //
        // TODO: Use a raw SYS_sigaction syscall in the first place, so that we
        // can know for sure that we're getting the kernel definition in the
        // handler. e.g. wrap the syscall in linux-api, and have the handler
        // specifically get a `linux_siginfo_t`.
        shimshmem_setThreadSiginfo(
            host_lock, shim_threadSharedMem(), signo, (linux_siginfo_t*)info);
    }

    shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
    shim_process_signals(void_ucontext);
    shim_swapAllowNativeSyscalls(oldNativeSyscallFlag);
}

void shim_install_hardware_error_handlers() {
    int error_signals[] = {
        SIGSEGV, SIGILL, SIGBUS, SIGFPE,
    };
    for (int i = 0; i < sizeof(error_signals) / sizeof(error_signals[0]); ++i) {
        if (sigaction(error_signals[i],
                      &(struct sigaction){
                          .sa_sigaction = shim_handle_hardware_error_signal,
                          // SA_NODEFER: Don't block the current signal in the handler.
                          // Generating one of these signals while it is blocked is
                          // undefined behavior; the handler itself detects recursion.
                          // SA_SIGINFO: Required because we're specifying
                          // sa_sigaction.
                          // SA_ONSTACK: Use the alternate signal handling stack,
                          // to avoid interfering with userspace thread stacks.
                          .sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK,
                      },
                      NULL) < 0) {
            panic("sigaction: %s", strerror(errno));
        }
    }
}