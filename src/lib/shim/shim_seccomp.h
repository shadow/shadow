/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_LIB_SHIM_SHIM_SECCOMP_H_
#define SRC_LIB_SHIM_SHIM_SECCOMP_H_

#include <sys/ucontext.h>

// Initialize the seccomp filter and syscall signal handler function.
void shim_seccomp_init();

#endif // SRC_LIB_SHIM_SHIM_SECCOMP_H_
