/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/process.h"

#include "main/host/syscall/protected.h"

SysCallReturn syscallhandler_execve(SysCallHandler* sys, const SysCallArgs* args) {
    // Notify the memorymanager that exec is about to be called.
    memorymanager_preExecHook(sys->memoryManager, sys->thread);

    // Have the plugin execute it natively.
    return (SysCallReturn){.state = SYSCALL_NATIVE};
}
