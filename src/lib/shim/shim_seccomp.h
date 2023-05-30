/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_LIB_SHIM_SHIM_SECCOMP_H_
#define SRC_LIB_SHIM_SHIM_SECCOMP_H_

#include <sys/ucontext.h>

// Initialize the seccomp filter and syscall signal handler function.
void shim_seccomp_init();

// Gets and resets the instruction pointer to which the child should resume
// execution after a clone syscall.
ucontext_t* shim_parent_thread_ctx();

// The name of the section from which the seccomp filter will allow syscalls.
// Must be consistent with the `extern char` symbols below.
#define SHADOW_ALLOW_NATIVE_SYSCALL_SECTION "shadow_allow_syscalls"

// These symbols are defined by the linker, and give the bounds of the code
// section `shadow_allow_syscalls`. If these become link errors, we may need to add/modify
// a linker script to explicitly define such symbols ourselves.
// https://stackoverflow.com/questions/4156585/how-to-get-the-length-of-a-function-in-bytes/22047976#comment83965391_22047976
//
// I wasn't able to find clear documentation from `ld` itself about how and
// when these symbols are generated, but
// [`ld(1)`](https://www.man7.org/linux/man-pages/man1/ld.1.html) does mention them in passing; e.g.
// the `start-stop-visibility` option controls their visibility.
//
// These names must be consistent with `SHADOW_ALLOW_NATIVE_SYSCALL_SECTION`, above.
extern char __start_shadow_allow_syscalls[];
extern char __stop_shadow_allow_syscalls[];

// Function attributes to add to a function so that the seccomp filter allows
// it to make direct syscalls (using inline asm).
//
// Example:
// ```
// __attribute__((SHADOW_ALLOW_NATIVE_SYSCALL_FN_ATTRS)) my_fn() {
//     pid_t tid = 0;
//     __asm__("syscall" : "=a"(tid) : "a"(SYS_gettid) :);
// }
// ```
#define SHADOW_ALLOW_NATIVE_SYSCALL_FN_ATTRS noinline, section(SHADOW_ALLOW_NATIVE_SYSCALL_SECTION)

#endif // SRC_LIB_SHIM_SHIM_SECCOMP_H_
