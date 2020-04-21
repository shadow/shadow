/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "preload/preload_functions.h"

#include <dlfcn.h>
#include <stddef.h>

void preload_functions_do_lookups(PreloadFuncs* vtable, void* handle) {
    if(vtable == NULL) {
        return;
    }

#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif
#define PRELOADDEF(returnstatement, returntype, functionname, argumentlist, ...) \
    vtable->functionname = dlsym(handle, #functionname); \
    dlerror();
//    vtable->functionname = dlsym(handle, functionname); \
//    char* errorMessage = dlerror(); \
//    if(errorMessage != NULL) { \
//        fprintf(stderr, "dlsym(symbol=%s, handle=%p): dlerror() message: %s\n", functionname, handle, errorMessage); \
//        exit(EXIT_FAILURE); \
//    } else if(funcptr == NULL) { \
//        fprintf(stderr, "dlsym(symbol=%s, handle=%p): returned NULL pointer\n", functionname, handle); \
//        exit(EXIT_FAILURE); \
//    } \

#include "preload/preload_defs_special.h"
#include "preload/preload_defs.h"

#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif
    return;
}
