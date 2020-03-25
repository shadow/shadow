#ifndef PRELOAD_NEXT_LIBC_H 
#define PRELOAD_NEXT_LIBC_H 

// Header-only library that provides the shim access to the system's libc functions
// that are otherwise overridden by the interposed definitions.
//
// Because this file is also transitively included by Shadow, we use
// conditional compilation so that it also works in that environment.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>

#ifdef SHADOW_SHIM
// In the Shim, we want the next definition.
#define SHADOW_SYSTEM_LIBC_HANDLE RTLD_NEXT
#else
// In Shadow, we want the first definition.
#define SHADOW_SYSTEM_LIBC_HANDLE RTLD_DEFAULT
#endif

#ifdef SHADOW_SHIM
#include "shim.h"
#define SHADOW_SYSTEM_LIBC_ERROR SHD_SHIM_LOG
#else
#include "support/logger/logger.h"
#define SHADOW_SYSTEM_LIBC_ERROR error
#endif

#define SHADOW_SYSTEM_LIBC_INIT(return_type, name, args)                       \
    static return_type(*system_libc_##name) args;                              \
    __attribute__((constructor)) static void _system_libc_init_##name() {      \
        system_libc_##name = dlsym(SHADOW_SYSTEM_LIBC_HANDLE, #name);          \
        if (system_libc_##name == NULL) {                                      \
            SHADOW_SYSTEM_LIBC_ERROR(                                          \
                "dlsym(%s): dlerror(): %s\n", #name, dlerror());                \
        }                                                                      \
    }

SHADOW_SYSTEM_LIBC_INIT(long, syscall, (long n, ...))
SHADOW_SYSTEM_LIBC_INIT(ssize_t, recv, (int a, void *b, size_t c, int flags));
SHADOW_SYSTEM_LIBC_INIT(ssize_t, send, (int a, const void *b, size_t c, int flags));

#undef SHADOW_SYSTEM_LIBC_HANDLE
#undef SHADOW_SYSTEM_LIBC_ERROR
#undef SHADOW_SYSTEM_LIBC_INIT

#endif
