/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_LIB_SHIM_SHIM_SECCOMP_H_
#define SRC_LIB_SHIM_SHIM_SECCOMP_H_

// Initialize the seccomp filter and syscall signal handler function.
void shim_seccomp_init();

// Gets and resets the instruction pointer to which the child should resume
// execution after a clone syscall.
void* shim_seccomp_take_clone_rip();

#endif // SRC_LIB_SHIM_SHIM_SECCOMP_H_
