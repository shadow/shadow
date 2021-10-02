/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stddef.h>
#include <syscall.h>

#include "lib/shim/shim_api.h"

/// The purpose of the injector library is to inject the shim into the process
/// space of each managed process, since the shim is needed for interaction with
/// Shadow. (Technically, the shim will already be injected if there are other
/// preloaded libraries that link to it, but this library provides a minimally
/// invasive way to inject the shim without preloading other symbols.)
///
/// One goal is to ensure the shim is injected, so we link to it and call one
/// shim function in a constructor. The other goal is to be minimally invasive,
/// so let's not define any other symbols here.

// A constructor is used to load the shim as soon as possible.
__attribute__((constructor, used)) void _injector_load() {
    // Make a call to the shim to ensure that it's loaded. The SYS_time syscall
    // will be handled locally in the shim, avoiding IPC with Shadow.
    shim_api_syscall(SYS_time, NULL);
    return;
}
