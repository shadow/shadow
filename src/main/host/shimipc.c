/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/shimipc.h"

#include "main/core/support/options.h"

// We use an int here because the option parsing library doesn't provide a way
// to set a boolean flag to false explicitly.
static gint _sendExplicitBlockMessage = true;
OPTION_EXPERIMENTAL_ENTRY(
    "send-explicit-block-message", 0, 0, G_OPTION_ARG_INT, &_sendExplicitBlockMessage,
    "Send message to plugin telling it to stop spinning when a syscall blocks", "[0|1]")

bool shimipc_sendExplicitBlockMessageEnabled() { return _sendExplicitBlockMessage; }

// Setting this too low will cause us to block too quickly, resulting in extra
// overhead from context switching.
//
// In principle setting this too high could result in hogging CPU cores when
// it'd be better to give them up.  However since Shadow explicitly tells
// plugins to stop spinning as soon as they make a blocking syscall, this is
// unlikely to happen. One scenario in which it *could* happen is if a plugin
// goes a long time without making any syscalls e.g. doing some very long
// pure-CPU-operation.
static gint _spinMax = 1000000000;
OPTION_EXPERIMENTAL_ENTRY("preload-spin-max", 0, 0, G_OPTION_ARG_INT, &_spinMax,
                          "Max number of iterations to busy-wait on ICP sempahore before blocking. "
                          "-1 for unlimited. [1000000000]",
                          "N")

ssize_t shimipc_spinMax() { return _spinMax; }
