#ifndef LIB_SHIM_SHADOW_SIGNALS_H
#define LIB_SHIM_SHADOW_SIGNALS_H

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>

#include "lib/logger/logger.h"

#define SHD_STANDARD_SIGNAL_MAX_NO 31

// Lowest and highest valid realtime signal, according to signal(7).  We don't
// use libc's SIGRTMIN and SIGRTMAX directly since those may omit some signal
// numbers that libc reserves for its internal use. We still need to handle
// those signal numbers in Shadow.
#define SHD_SIGRT_MIN 32
#define SHD_SIGRT_MAX 64

// Definition is sometimes missing in the userspace headers. We could include
// the kernel signal header, but it has definitions that conflict with the
// userspace headers.
#ifndef SS_AUTODISARM
#define SS_AUTODISARM (1U << 31)
#endif

// Compatible with the kernel's definition of sigset_t on x86_64. Exposing the
// definition in the header so that it can be used as a value-type, but should
// be manipulated with the helpers below.
//
// This is analagous to but typically smaller than libc's sigset_t.
typedef struct {
    uint64_t val;
} shd_kernel_sigset_t;

// Compatible with kernel's definition of `struct sigaction`. Different from libc's in that
// `ksa_handler` and `ksa_sigaction` are explicitly in a union, and that `ksa_mask` is the
// kernel's mask size (64 bits) vs libc's larger one (~1000 bits for glibc).
//
// We use the field prefix ksa_ to avoid conflicting with macros defined for the
// corresponding field names in glibc.
struct shd_kernel_sigaction {
    union {
        void (*ksa_handler)(int);
        void (*ksa_sigaction)(int, siginfo_t*, void*);
    };
    int ksa_flags;
    void (*ksa_restorer)(void);
    shd_kernel_sigset_t ksa_mask;
};

// Corresponds to default actions documented in signal(7).
typedef enum {
    SHD_DEFAULT_ACTION_TERM,
    SHD_DEFAULT_ACTION_IGN,
    SHD_DEFAULT_ACTION_CORE,
    SHD_DEFAULT_ACTION_STOP,
    SHD_DEFAULT_ACTION_CONT,
} ShdKernelDefaultAction;

// Returns default action documented in signal(7) for the given signal.
ShdKernelDefaultAction shd_defaultAction(int signo);

shd_kernel_sigset_t shd_sigemptyset();
shd_kernel_sigset_t shd_sigfullset();
void shd_sigaddset(shd_kernel_sigset_t* set, int signum);
void shd_sigdelset(shd_kernel_sigset_t* set, int signum);
bool shd_sigismember(const shd_kernel_sigset_t* set, int signum);
bool shd_sigisemptyset(const shd_kernel_sigset_t* set);
shd_kernel_sigset_t shd_sigorset(const shd_kernel_sigset_t* left, const shd_kernel_sigset_t* right);
shd_kernel_sigset_t shd_sigandset(const shd_kernel_sigset_t* left,
                                  const shd_kernel_sigset_t* right);
shd_kernel_sigset_t shd_signotset(const shd_kernel_sigset_t* src);

// Return the smallest signal number that's set, or 0 if none are.
int shd_siglowest(const shd_kernel_sigset_t* set);

#endif