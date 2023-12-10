/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_SYSCALL_KERNEL_TYPES_H_
#define SRC_MAIN_HOST_SYSCALL_KERNEL_TYPES_H_

/* See `man 2 getdents`:
 *   Glibc does not provide a wrapper for these system calls; call them
 *   using syscall(2).  You will need to define the linux_dirent or
 *   linux_dirent64 structure yourself.
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SUID_DUMP_DISABLE
#define SUID_DUMP_DISABLE 0
#endif

#ifndef SUID_DUMP_USER
#define SUID_DUMP_USER 1
#endif

struct linux_dirent;
struct linux_dirent64;

#endif /* SRC_MAIN_HOST_SYSCALL_KERNEL_TYPES_H_ */
