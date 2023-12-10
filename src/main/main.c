/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "lib/log-c2rust/log-c2rust.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "shd-build-info.h"

int main(int argc, char* argv[]) {
    // Initialize C code logger, which delegates to the Rust logger.  This won't
    // actually do much until the Rust logger is initialized in
    // `main_runShadow`.
    logger_setDefault(rustlogger_new());

    return main_runShadow(argc, (const char**)argv);
}
