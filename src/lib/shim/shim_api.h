#ifndef SRC_LIB_SHIM_SHIM_API_H_
#define SRC_LIB_SHIM_SHIM_API_H_

#include <stdarg.h>

// The entry point for handling an intercepted syscall. This function remaps the
// return value into errno upon error so that errno will be set correctly upon
// returning control to the managed process.
long shim_api_syscall(long n, ...);

// Same as `shim_api_syscall()`, but allows a variable arguments list.
long shim_api_syscallv(long n, va_list args);

#endif // SRC_LIB_SHIM_SHIM_API_H_