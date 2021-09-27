#include <stdarg.h>
#include <sys/syscall.h>

// Temporary external def; TODO replace with appropriate include
long shim_syscallv(long n, va_list args);

long syscall(long n, ...) {
    va_list(args);
    va_start(args, n);
    long rv = shim_syscallv(n, args);
    va_end(args);
    return rv;
}
