#include "lib/shim/shim_syscall.h"

#include <alloca.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_api.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_sys.h"
#include "lib/shim/shim_tls.h"
#include "main/host/syscall_numbers.h"

// Handle to the real syscall function, initialized once at load-time for
// thread-safety.
long shim_native_syscall(ucontext_t* ctx, long n, ...) {
    va_list args;
    va_start(args, n);
    long rv = shim_native_syscallv(n, args);
    va_end(args);
    return rv;
}

long shim_emulated_syscall(ucontext_t* ctx, long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_emulated_syscallv(ctx, n, args);
    va_end(args);
    return rv;
}

long shim_syscallv(ucontext_t* ctx, long n, va_list args) {
    shim_ensure_init();

    long rv;

    if (shim_getExecutionContext() == EXECUTION_CONTEXT_APPLICATION &&
        shim_sys_handle_syscall_locally(n, &rv, args)) {
        // No inter-process syscall needed, we handled it on the shim side! :)
        trace("Handled syscall %ld from the shim; we avoided inter-process overhead.", n);
        // rv was already set
    } else if ((shim_getExecutionContext() == EXECUTION_CONTEXT_APPLICATION ||
                syscall_num_is_shadow(n)) &&
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

long shim_syscall(ucontext_t* ctx, long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_syscallv(ctx, n, args);
    va_end(args);
    return rv;
}
