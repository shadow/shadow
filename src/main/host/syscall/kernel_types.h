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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/types.h>
#include <unistd.h>

#include "shd-config.h"

// clang-format off
#if defined(HAVE_STRUCT_LINUX_DIRENT) && HAVE_STRUCT_LINUX_DIRENT == 0
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

#if defined(HAVE_STRUCT_LINUX_DIRENT64) && HAVE_STRUCT_LINUX_DIRENT64 == 0
struct linux_dirent64 {
   ino64_t        d_ino;    /* 64-bit inode number */
   off64_t        d_off;    /* 64-bit offset to next structure */
   unsigned short d_reclen; /* Size of this dirent */
   unsigned char  d_type;   /* File type */
   char           d_name[]; /* Filename (null-terminated) */
};
#endif

/* See `man 2 statx`:
 *   statx() was added to Linux in kernel 4.11; library support was added in
 *   glibc 2.28.
 */

#if defined(HAVE_STRUCT_STATX_TIMESTAMP) && HAVE_STRUCT_STATX_TIMESTAMP == 0
typedef unsigned int __u32;
typedef long long __s64;
struct statx_timestamp {
       __s64 tv_sec;    /* Seconds since the Epoch (UNIX time) */
       __u32 tv_nsec;   /* Nanoseconds since tv_sec */
};
#endif

#if defined(HAVE_STRUCT_STATX) && HAVE_STRUCT_STATX == 0
typedef unsigned short __u16;
typedef unsigned int __u32;
typedef long long __u64;
struct statx {
   __u32 stx_mask;        /* Mask of bits indicating
                             filled fields */
   __u32 stx_blksize;     /* Block size for filesystem I/O */
   __u64 stx_attributes;  /* Extra file attribute indicators */
   __u32 stx_nlink;       /* Number of hard links */
   __u32 stx_uid;         /* User ID of owner */
   __u32 stx_gid;         /* Group ID of owner */
   __u16 stx_mode;        /* File type and mode */
   __u64 stx_ino;         /* Inode number */
   __u64 stx_size;        /* Total size in bytes */
   __u64 stx_blocks;      /* Number of 512B blocks allocated */
   __u64 stx_attributes_mask;
                          /* Mask to show what's supported
                             in stx_attributes */

   /* The following fields are file timestamps */
   struct statx_timestamp stx_atime;  /* Last access */
   struct statx_timestamp stx_btime;  /* Creation */
   struct statx_timestamp stx_ctime;  /* Last status change */
   struct statx_timestamp stx_mtime;  /* Last modification */

   /* If this file represents a device, then the next two
      fields contain the ID of the device */
   __u32 stx_rdev_major;  /* Major ID */
   __u32 stx_rdev_minor;  /* Minor ID */

   /* The next two fields contain the ID of the device
      containing the filesystem where the file resides */
   __u32 stx_dev_major;   /* Major ID */
   __u32 stx_dev_minor;   /* Minor ID */
};
#endif
//clang-format on

#endif /* SRC_MAIN_HOST_SYSCALL_KERNEL_TYPES_H_ */
