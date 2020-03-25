#ifndef PRELOAD_NEXT_LIBC_H
#define PRELOAD_NEXT_LIBC_H

// Header-only library that provides the shim access to the system's libc
// functions that are otherwise overridden by the interposed definitions.
//
// Because this file is also transitively included by Shadow, we use
// conditional compilation so that it also works in that environment.

#ifdef SHADOW_SHIM

// Forward declare global function pointers system_libc_X, which will point to
// the respective functions X.
#define SYSTEM_LIBC_FN(name, return_type, args)                                \
    extern return_type(*system_libc_##name) args;
#include "system-libc-fns.inc.h"
#undef SYSTEM_LIBC_FN


#else // SHADOW_SHIM

// When adding a function here, add it also to system-libc-fns.inc.h
// (Unfortunately the xmacro trick used above doesn't work here, so we have to
// repeat ourselves.)
#define system_libc_abort abort
#define system_libc_recv recv
#define system_libc_send send
#define system_libc_syscall syscall

#endif // SHADOW_SHIM

#endif
