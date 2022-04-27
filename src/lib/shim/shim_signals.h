#ifndef LIB_SHIM_SHIM_SIGNALS_H
#define LIB_SHIM_SHIM_SIGNALS_H

#include <stdbool.h>

#include "lib/shim/shim_shmem.h"

// Handle pending unblocked signals, and return whether *all* corresponding
// signal actions had the SA_RESTART flag set.
//
// `ucontext` will be passed through to handlers if non-NULL. This should
// generally only be done if the caller has a `ucontext` that will be swapped to
// after this code returns; e.g. one that was passed to our own signal handler,
// which will be swapped to when that handler returns.
//
// If `ucontext` is NULL, one will be created at the point where we invoke
// the handler, and swapped back to when it returns.
// TODO: Creating `ucontext_t` is currently only implemented for handlers that
// execute on a sigaltstack.
bool shim_process_signals(ShimShmemHostLock* host_lock, ucontext_t* context);

#endif