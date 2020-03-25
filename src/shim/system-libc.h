#ifndef PRELOAD_NEXT_LIBC_H 
#define PRELOAD_NEXT_LIBC_H 

// Header-only library that provides the shim access to the system's libc functions
// that are otherwise overridden by the interposed definitions.
//
// Because this file is also transitively included by Shadow, we use
// conditional compilation so that it also works in that environment.

#ifdef SHADOW_SHIM

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

#include "shim.h"


#define SHADOW_SYSTEM_LIBC_INIT(return_type, name, args)                       \
    static return_type(*system_libc_##name) args;                              \
    __attribute__((constructor)) static void _system_libc_init_##name() {      \
        system_libc_##name = dlsym(RTLD_NEXT, #name);                          \
        if (system_libc_##name == NULL) {                                      \
            SHD_SHIM_LOG("dlsym(%s): dlerror(): %s\n", #name, dlerror());      \
        }                                                                      \
    }

SHADOW_SYSTEM_LIBC_INIT(long, syscall, (long n, ...))
SHADOW_SYSTEM_LIBC_INIT(ssize_t, recv, (int a, void *b, size_t c, int flags));
SHADOW_SYSTEM_LIBC_INIT(ssize_t, send, (int a, const void *b, size_t c, int flags));

#else // SHADOW_SHIM

#define system_libc_syscall syscall
#define system_libc_recv recv 
#define system_libc_send send 

#endif // SHADOW_SHIM

#endif
