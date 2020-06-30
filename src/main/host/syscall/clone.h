/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"

SysCallReturn syscallhandler_clone(SysCallHandler* sys,
                                   const SysCallArgs* args);
