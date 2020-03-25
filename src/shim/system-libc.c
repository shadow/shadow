#include "shim.h"

#include <dlfcn.h>
#include <stdlib.h>

// Declare the function pointers, and constructor functions to initialize them.
#define SYSTEM_LIBC_FN(name, return_type, args)                                \
    return_type(*system_libc_##name) args;                                     \
    __attribute__((constructor(200))) static void _system_libc_init_##name() { \
        system_libc_##name = dlsym(RTLD_NEXT, #name);                          \
        if (system_libc_##name == NULL) {                                      \
            SHD_SHIM_LOG("dlsym(%s): dlerror(): %s\n", #name, dlerror());      \
            system_libc_abort();                                               \
        }                                                                      \
    }
#include "system-libc-fns.inc.h"
#undef SYSTEM_LIBC_FN
