/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_SELECT_H_
#define SRC_MAIN_HOST_SYSCALL_SELECT_H_

#include "main/host/syscall/protected.h"

SYSCALL_HANDLER(select);
SYSCALL_HANDLER(pselect6);

#endif /* SRC_MAIN_HOST_SYSCALL_SELECT_H_ */
