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

static gint _spinMax = 8096;
OPTION_EXPERIMENTAL_ENTRY("preload-spin-max", 0, 0, G_OPTION_ARG_INT, &_spinMax,
                          "Max number of iterations to busy-wait on ICP sempahore before blocking. "
                          "-1 for unlimited. [8096]",
                          "N")

ssize_t shimipc_spinMax() { return _spinMax; }
