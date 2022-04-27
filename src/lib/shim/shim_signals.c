#include "lib/shim/shim_signals.h"

#include "lib/shim/shim.h"

#include <ucontext.h>

static void _call_signal_handler(const struct shd_kernel_sigaction* action, int signo,
                                 siginfo_t* siginfo, ucontext_t* ucontext) {
    shim_swapAllowNativeSyscalls(false);
    if (action->ksa_flags & SA_SIGINFO) {
        action->ksa_sigaction(signo, siginfo, ucontext);
    } else {
        action->ksa_handler(signo);
    }
    shim_swapAllowNativeSyscalls(true);
}

// Handle pending unblocked signals, and return whether *all* corresponding
// signal actions had the SA_RESTART flag set.
bool shim_process_signals(ShimShmemHostLock* host_lock, ucontext_t* ucontext) {
    int signo;
    siginfo_t siginfo;
    bool restartable = true;
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
        shimshmem_setBlockedSignals(host_lock, shim_threadSharedMem(), handler_mask);

        if (action.ksa_flags & SA_RESETHAND) {
            shimshmem_setSignalAction(host_lock, shim_processSharedMem(), signo,
                                      &(struct shd_kernel_sigaction){.ksa_handler = SIG_DFL});
        }
        if (!(action.ksa_flags & SA_RESTART)) {
            restartable = false;
        }

        const stack_t ss_original = shimshmem_getSigAltStack(host_lock, shim_threadSharedMem());
        if (action.ksa_flags & SA_ONSTACK && !(ss_original.ss_flags & SS_DISABLE)) {
            // Call handler on the configured signal stack.

            if (ss_original.ss_flags & SS_ONSTACK) {
                // Documentation is unclear what should happen, but switching to
                // the already-in-use stack would almost certainly go badly.
                panic("Alternate stack already in use.")
            }

            // Update the signal-stack configuration while the handler is being run.
            stack_t ss_during_handler;
            if (ss_original.ss_flags & SS_AUTODISARM) {
                ss_during_handler = (stack_t){.ss_flags = SS_DISABLE};
            } else {
                ss_during_handler = ss_original;
                ss_during_handler.ss_flags |= SS_ONSTACK;
            }
            shimshmem_setSigAltStack(host_lock, shim_threadSharedMem(), ss_during_handler);

            // Set up a context that uses the configured signal stack.
            ucontext_t orig_ctx = {0}, handler_ctx = {0};
            getcontext(&handler_ctx);
            handler_ctx.uc_link = &orig_ctx;
            handler_ctx.uc_stack.ss_sp = ss_original.ss_sp;
            handler_ctx.uc_stack.ss_size = ss_original.ss_size;
            // If a context was provided by the caller, we pass that through
            // to the signal handler; it's the caller's responsibility to swap
            // back to that context.
            //
            // Otherwise we pass the pre-stack-switch context we're creating
            // here.  It'll be swapped-back-to when `swapcontext` returns.
            ucontext_t* ctx = ucontext ? ucontext : &orig_ctx;
            makecontext(&handler_ctx, (void (*)(void))_call_signal_handler, 4, &action, signo,
                        &siginfo, ctx);

            // Call the handler on the configured signal stack.
            shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
            if (swapcontext(&orig_ctx, &handler_ctx) != 0) {
                panic("swapcontext: %s", strerror(errno));
            }
            host_lock = shimshmemhost_lock(shim_hostSharedMem());

            // Restore the signal-stack configuration.
            shimshmem_setSigAltStack(host_lock, shim_threadSharedMem(), ss_original);
        } else {
            ucontext_t* ctx = ucontext;
            if (ctx == NULL) {
                // To handle this case we might be able to use `makecontext`
                // and `swapcontext` as in the sigaltstack case, but we'd need
                // a stack to use for the new context. We could try to partition
                // the current stack, but that's a bit tricky.
                //
                // So far we don't know of any real-world cases that get here
                // and actually dereference the context in the handler.
                debug("Passing NULL ucontext_t to handler for signal %d", signo);
            }

            // Call signal handler with host lock released, and native syscalls
            // disabled.
            shimshmemhost_unlock(shim_hostSharedMem(), &host_lock);
            _call_signal_handler(&action, signo, &siginfo, ctx);
            host_lock = shimshmemhost_lock(shim_hostSharedMem());
        }

        // Restore mask
        shimshmem_setBlockedSignals(host_lock, shim_threadSharedMem(), blocked_signals);
    }
    return restartable;
}
