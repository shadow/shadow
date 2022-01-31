#include "shadow_signals.h"

ShdKernelDefaultAction shd_defaultAction(int signo) {
    switch (signo) {
        case SIGCONT: return SHD_DEFAULT_ACTION_CONT;
        // aka SIGIOT
        case SIGABRT:
        case SIGBUS:
        case SIGFPE:
        case SIGILL:
        case SIGQUIT:
        case SIGSEGV:
        case SIGSYS:
        case SIGTRAP:
        case SIGXCPU:
        case SIGXFSZ: return SHD_DEFAULT_ACTION_CORE;
        // aka SIGCLD
        case SIGCHLD:
        case SIGURG:
        case SIGWINCH: return SHD_DEFAULT_ACTION_IGN;
        case SIGSTOP:
        case SIGTSTP:
        case SIGTTIN:
        case SIGTTOU: return SHD_DEFAULT_ACTION_STOP;
        case SIGALRM:
#ifdef SIGEMT
        case SIGEMT:
#endif
        case SIGHUP:
        case SIGINT:
        // aka SIGPOLL
        case SIGIO:
        case SIGKILL:
#ifdef SIGLOST
        case SIGLOST:
#endif
        case SIGPIPE:
        case SIGPROF:
        case SIGPWR:
        case SIGSTKFLT:
        case SIGTERM:
        case SIGUSR1:
        case SIGUSR2:
        case SIGVTALRM: return SHD_DEFAULT_ACTION_TERM;
        default: error("Unrecognized signal %d", signo); return SHD_DEFAULT_ACTION_CORE;
    }
}

shd_kernel_sigset_t shd_sigemptyset() { return (shd_kernel_sigset_t){0}; }

shd_kernel_sigset_t shd_sigfullset() { return (shd_kernel_sigset_t){.val = ~UINT64_C(0)}; }

void shd_sigaddset(shd_kernel_sigset_t* set, int signum) {
    if (signum < 1 || signum > SHD_SIGRT_MAX) {
        panic("Bad signum %d", signum);
    }
    set->val |= UINT64_C(1) << (signum - 1);
}

void shd_sigdelset(shd_kernel_sigset_t* set, int signum) {
    if (signum < 1 || signum > SHD_SIGRT_MAX) {
        panic("Bad signum %d", signum);
    }
    set->val &= ~(UINT64_C(1) << (signum - 1));
}

bool shd_sigismember(const shd_kernel_sigset_t* set, int signum) {
    if (signum < 1 || signum > SHD_SIGRT_MAX) {
        return false;
    }
    return set->val & (UINT64_C(1) << (signum - 1));
}

bool shd_sigisemptyset(const shd_kernel_sigset_t* set) { return set->val == 0; }

shd_kernel_sigset_t shd_sigorset(const shd_kernel_sigset_t* left,
                                 const shd_kernel_sigset_t* right) {
    return (shd_kernel_sigset_t){.val = left->val | right->val};
}

shd_kernel_sigset_t shd_sigandset(const shd_kernel_sigset_t* left,
                                  const shd_kernel_sigset_t* right) {
    return (shd_kernel_sigset_t){.val = left->val & right->val};
}

shd_kernel_sigset_t shd_signotset(const shd_kernel_sigset_t* src) {
    return (shd_kernel_sigset_t){.val = ~src->val};
}

// Return the smallest signal number that's set, or 0 if none are.
int shd_siglowest(const shd_kernel_sigset_t* set) {
    if (!set->val) {
        return 0;
    }
    // Naive loop for now. There's probably some more clever bit manipulation we could do.
    for (int i = 1; i <= SHD_SIGRT_MAX; ++i) {
        if (shd_sigismember(set, i)) {
            return i;
        }
    }
    // Unreachable
    panic("Unreachable");
}
