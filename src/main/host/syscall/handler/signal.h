/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef MAIN_HOST_SYSCALL_SIGNAL_H
#define MAIN_HOST_SYSCALL_SIGNAL_H

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(kill);
SYSCALL_HANDLER(tgkill);
SYSCALL_HANDLER(tkill);
SYSCALL_HANDLER(rt_sigaction);
SYSCALL_HANDLER(rt_sigprocmask);
SYSCALL_HANDLER(sigaltstack);

#endif
