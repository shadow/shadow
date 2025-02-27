/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#include "lib/shim/shim_api_c.h"

#include <errno.h>
#include <stdarg.h>

#include "lib/shim/shim_syscall.h"

// Make sure we don't call any syscalls ourselves after this function is called, otherwise
// the errno that we set here could get overwritten before we return to the plugin.
static long _shim_api_retval_to_errno(long retval) {
    // Linux reserves -1 through -4095 for errors. See
    // https://sourceware.org/git/?p=glibc.git;a=blob;f=sysdeps/unix/sysv/linux/x86_64/sysdep.h;h=24d8b8ec20a55824a4806f8821ecba2622d0fe8e;hb=HEAD#l41
    if (retval <= -1 && retval >= -4095) {
        errno = (int)-retval;
        return -1;
    }
    return retval;
}

long shimc_api_syscall(ExecutionContext ctx, long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_syscallv(NULL, ctx, n, args);
    va_end(args);
    return _shim_api_retval_to_errno(rv);
}
