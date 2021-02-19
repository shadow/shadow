#ifndef SHIM_SHIM_LOGGER_H_
#define SHIM_SHIM_LOGGER_H_

#include <inttypes.h>

#include "support/logger/logger.h"

#include <stdio.h>

Logger* shimlogger_new(FILE* file);

// Caches the current simulation time to avoid invoking syscalls to get it.
// Not thread safe, but doesn't matter since Shadow only permits
// one thread at a time to run anyway.
void shimlogger_set_simulation_nanos(uint64_t simulation_nanos);

// Returns the current cached simulation time, or 0 if it has not yet been set.
uint64_t shimlogger_get_simulation_nanos();

#endif
