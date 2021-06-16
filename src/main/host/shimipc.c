/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/host/shimipc.h"

static bool _useExplicitBlockMessage = true;
ADD_CONFIG_HANDLER(config_getUseExplicitBlockMessage, _useExplicitBlockMessage)

bool shimipc_sendExplicitBlockMessageEnabled() { return _useExplicitBlockMessage; }

static int _spinMax = -1;
ADD_CONFIG_HANDLER(config_getPreloadSpinMax, _spinMax)

static IpcMethod _ipcMethod;
ADD_CONFIG_HANDLER(config_getIpcMethod, _ipcMethod)
IpcMethod shimipc_getIpcMethod() { return _ipcMethod; }

ssize_t shimipc_spinMax() { return _spinMax; }
