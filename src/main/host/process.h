/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PROCESS_H_
#define SHD_PROCESS_H_

#include <dlfcn.h>
#include <fcntl.h>
#include <features.h>
#include <glib.h>
#include <ifaddrs.h>
#include <linux/sockios.h>
#include <malloc.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/file.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

// A wrapper around GLib's `g_shell_parse_argv()` that doesn't use GLib types. The returned
// pointers must be freed using `process_parseArgStrFree()`.
bool process_parseArgStr(const char* commandLine, int* argc, char*** argv, char** error);
// Free all data allocated by `process_parseArgStr()`.
void process_parseArgStrFree(char** argv, char* error);

// Helper for the Rust Process. `siginfo_t` is difficult to initialize from Rust,
// due to opaque fields and macro magic in its C definition.
void process_initSiginfoForAlarm(siginfo_t* siginfo, int overrun);

#endif /* SHD_PROCESS_H_ */
