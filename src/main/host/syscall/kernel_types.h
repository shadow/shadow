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

#include "shd-config.h"

#ifndef SUID_DUMP_DISABLE
#define SUID_DUMP_DISABLE 0
#endif

#ifndef SUID_DUMP_USER
#define SUID_DUMP_USER 1
#endif

#if HAVE_STRUCT_LINUX_DIRENT == 0
struct linux_dirent {
   unsigned long  d_ino;     /* Inode number */
   unsigned long  d_off;     /* Offset to next linux_dirent */
   unsigned short d_reclen;  /* Length of this linux_dirent */
   char           d_name[];  /* Filename (null-terminated) */
                     /* length is actually (d_reclen - 2 -
                        offsetof(struct linux_dirent, d_name)) */
   /*
   char           pad;       // Zero padding byte
   char           d_type;    // File type (only since Linux
                             // 2.6.4); offset is (d_reclen - 1)
   */
};
#endif

#if HAVE_STRUCT_LINUX_DIRENT64 == 0
struct linux_dirent64 {
   ino64_t        d_ino;    /* 64-bit inode number */
   off64_t        d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};
#endif

#endif /* SRC_MAIN_HOST_SYSCALL_KERNEL_TYPES_H_ */
