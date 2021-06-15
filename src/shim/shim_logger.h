#ifndef SHIM_SHIM_LOGGER_H_
#define SHIM_SHIM_LOGGER_H_

#include "support/logger/logger.h"

#include <stdio.h>

Logger* shimlogger_new(FILE* file);

#endif
