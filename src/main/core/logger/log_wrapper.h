#include "lib/logger/logger.h"

// Create a logger that delegates to Rust's `log` crate.
Logger* rustlogger_new();

void rustlogger_destroy(Logger* logger);