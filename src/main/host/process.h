/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROCESS_H_
#define SHD_PROCESS_H_

#include <signal.h>
#include <stdbool.h>

// A wrapper around GLib's `g_shell_parse_argv()` that doesn't use GLib types. The returned
// pointers must be freed using `process_parseArgStrFree()`.
bool process_parseArgStr(const char* commandLine, int* argc, char*** argv, char** error);
// Free all data allocated by `process_parseArgStr()`.
void process_parseArgStrFree(char** argv, char* error);

// Helper for the Rust Process. `siginfo_t` is difficult to initialize from Rust,
// due to opaque fields and macro magic in its C definition.
void process_initSiginfoForAlarm(siginfo_t* siginfo, int overrun);

#endif /* SHD_PROCESS_H_ */
