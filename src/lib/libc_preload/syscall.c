#include <stdarg.h>
#include <sys/syscall.h>

#include "lib/shim/shim_api.h"

long syscall(long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_api_syscallv(n, args);
    va_end(args);
    return rv;
}
