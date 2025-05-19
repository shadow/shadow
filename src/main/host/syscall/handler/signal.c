/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/syscall.h>

#include "lib/linux-api/linux-api.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/syscall/protected.h"
#include "main/host/syscall/syscall_condition.h"
#include "main/utility/syscall.h"

// Signals for which the shim installs a signal handler. We don't let managed
// code override the handler or change the disposition of these signals.
//
// SIGSYS: Used to catch and handle syscalls via seccomp.
// SIGSEGV: Used to catch and handle usage of rdtsc and rdtscp.
static int _shim_handled_signals[] = {SIGSYS, SIGSEGV};

#ifndef ARRAY_LENGTH
#define ARRAY_LENGTH(x) (sizeof(x) / sizeof((x)[0]))
#endif
