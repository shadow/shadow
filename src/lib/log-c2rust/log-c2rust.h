#ifndef LOG_C2RUST_H
#define LOG_C2RUST_H

#include "lib/logger/logger.h"

// Create a logger that delegates to Rust's `log` crate.
Logger* rustlogger_new();

void rustlogger_destroy(Logger* logger);

#endif