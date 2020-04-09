/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_PRELOAD_SHD_PRELOAD_FUNCTIONS_H_
#define SRC_PRELOAD_SHD_PRELOAD_FUNCTIONS_H_

#include "shd-preload-includes.h"

#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif
#define PRELOADDEF(returnstatement, returntype, functionname, argumentlist, ...) \
typedef returntype (*functionname ## _func)argumentlist;

#include "shd-preload-defs-special.h"
#include "shd-preload-defs.h"

#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif

typedef struct {
#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif
#define PRELOADDEF(returnstatement, returntype, functionname, argumentlist, ...) \
    functionname##_func functionname;

#include "shd-preload-defs-special.h"
#include "shd-preload-defs.h"

#if defined(PRELOADDEF)
#undef PRELOADDEF
#endif
} PreloadFuncs;

void preload_functions_do_lookups(PreloadFuncs* vtable, void* handle);

#endif /* SRC_PRELOAD_SHD_PRELOAD_FUNCTIONS_H_ */
