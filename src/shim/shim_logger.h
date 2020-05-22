#ifndef SHIM_SHIM_LOGGER_H_
#define SHIM_SHIM_LOGGER_H_

#include <inttypes.h>

#include "support/logger/logger.h"

#include <stdio.h>

Logger* shimlogger_new(FILE* file);

// Not thread safe, but doesn't matter since Shadow only permits
// one thread at a time to run anyway.
void shimlogger_set_simulation_nanos(uint64_t simulation_nanos);

#endif
