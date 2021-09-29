/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stddef.h>
#include <syscall.h>

#include "lib/shim/shim_api.h"

// The purpose of the dummy library is to load the shim as a dependency in case
// there are no other preload libs in use that would load it. This lib is
// preloaded, so we do not want to define other symbols here that could cause
// unintended interceptions.
__attribute__((constructor, used)) void _dummy_load() {
    // Make a call to the shim to ensure that it's loaded. The SYS_time syscall
    // will be handled locally in the shim, avoiding IPC with Shadow.
    shim_api_syscall(SYS_time, NULL);
    return;
}
