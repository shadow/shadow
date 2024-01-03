/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_FILE_H_
#define SRC_MAIN_HOST_DESCRIPTOR_FILE_H_

#include <poll.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "main/core/definitions.h"
#include "main/host/syscall/kernel_types.h"

/* Mask of all O file flags that we don't pass to the native fd, but instead
 * track within Shadow and handle manually. */
extern const int SHADOW_FLAG_MASK;

/* Opaque type representing a file-backed file descriptor. */
typedef struct _RegularFile RegularFile;

typedef enum _FileType FileType;
enum _FileType {
    FILE_TYPE_NOTSET,
    FILE_TYPE_REGULAR,
    FILE_TYPE_RANDOM,    // special handling for /dev/random etc.
    FILE_TYPE_HOSTS,     // special handling for /etc/hosts
    FILE_TYPE_LOCALTIME, // special handling for /etc/localtime
    FILE_TYPE_IN_MEMORY, // special handling for emulated files like /sys/*
};

/* In order to operate on a file, you must first create one with regularfile_new()
 * and open it with either regularfile_open() or regularfile_openat(). Internally, we use
 * OS-backed files to support the Shadow file descriptor API.
 *
 * There are two main types of functions supported by this API:
 * - The first set operates on the given RegularFile*. The file must have been
 *   created and the RegularFile* must be non-null.
 * - The second set operates on paths, and optionally includes a file object
 *   that represents a directory from which a relative path is computed.
 *   These calls usually end with "at". The directory RegularFile* can be null, in
 *   which case the current working directory (AT_FDCWD) will be used instead.
 */

// ************************
// Initialization and setup
// ************************

RegularFile* regularfile_new(); // Close the file with legacyfile_close()
int regularfile_open(RegularFile* file, const char* pathname, int flags, mode_t mode,
                     const char* workingDir);
int regularfile_openat(RegularFile* file, RegularFile* dir, const char* pathname, int flags,
                       mode_t mode, const char* workingDir);

// ************************
// Accessors
// ************************

/* Returns the flags that were used when opening the file. */
int regularfile_getFlagsAtOpen(RegularFile* file);

/* Returns the mode that was used when opening the file. */
mode_t regularfile_getModeAtOpen(RegularFile* file);

/* Get the file flags that shadow handles manually, but not the flags on the
 * linux-backed file. Will be a subset of SHADOW_FLAG_MASK. */
int regularfile_getShadowFlags(RegularFile* file);

/* Get the type of file. */
FileType regularfile_getType(RegularFile* file);

/* Returns the linux-backed fd that shadow uses to perform the file operations.  */
int regularfile_getOSBackedFD(RegularFile* file);

// ****************************************
// Operations that require a non-null RegularFile*
// ****************************************

ssize_t regularfile_read(RegularFile* file, const Host* host, void* buf, size_t bufSize);
ssize_t regularfile_pread(RegularFile* file, const Host* host, void* buf, size_t bufSize,
                          off_t offset);
ssize_t regularfile_preadv(RegularFile* file, const Host* host, const struct iovec* iov, int iovcnt,
                           off_t offset);
#ifdef SYS_preadv2
ssize_t regularfile_preadv2(RegularFile* file, const Host* host, const struct iovec* iov,
                            int iovcnt, off_t offset, int flags);
#endif
ssize_t regularfile_write(RegularFile* file, const void* buf, size_t bufSize);
ssize_t regularfile_pwrite(RegularFile* file, const void* buf, size_t bufSize, off_t offset);
ssize_t regularfile_pwritev(RegularFile* file, const struct iovec* iov, int iovcnt, off_t offset);
#ifdef SYS_pwritev2
ssize_t regularfile_pwritev2(RegularFile* file, const struct iovec* iov, int iovcnt, off_t offset,
                             int flags);
#endif
int regularfile_fstat(RegularFile* file, struct stat* statbuf);
int regularfile_fstatfs(RegularFile* file, struct statfs* statbuf);
int regularfile_fsync(RegularFile* file);
int regularfile_fchown(RegularFile* file, uid_t owner, gid_t group);
int regularfile_fchmod(RegularFile* file, mode_t mode);
int regularfile_ftruncate(RegularFile* file, off_t length);
int regularfile_fallocate(RegularFile* file, int mode, off_t offset, off_t length);
int regularfile_fadvise(RegularFile* file, off_t offset, off_t len, int advice);
int regularfile_flock(RegularFile* file, int operation);
int regularfile_fsetxattr(RegularFile* file, const char* name, const void* value, size_t size,
                          int flags);
ssize_t regularfile_fgetxattr(RegularFile* file, const char* name, void* value, size_t size);
ssize_t regularfile_flistxattr(RegularFile* file, char* list, size_t size);
int regularfile_fremovexattr(RegularFile* file, const char* name);
int regularfile_sync_range(RegularFile* file, off64_t offset, off64_t nbytes, unsigned int flags);
ssize_t regularfile_readahead(RegularFile* file, off64_t offset, size_t count);
off_t regularfile_lseek(RegularFile* file, off_t offset, int whence);
int regularfile_getdents(RegularFile* file, struct linux_dirent* dirp, unsigned int count);
int regularfile_getdents64(RegularFile* file, struct linux_dirent64* dirp, unsigned int count);
int regularfile_ioctl(RegularFile* file, unsigned long request, void* arg);
int regularfile_fcntl(RegularFile* file, unsigned long command, void* arg);
int regularfile_poll(RegularFile* file, struct pollfd* pfd);

// ******************************************
// Operations where the dir RegularFile* may be null
// ******************************************

int regularfile_fstatat(RegularFile* dir, const char* pathname, struct stat* statbuf, int flags,
                        const char* workingDir);
int regularfile_fchownat(RegularFile* dir, const char* pathname, uid_t owner, gid_t group,
                         int flags, const char* workingDir);
int regularfile_fchmodat(RegularFile* dir, const char* pathname, mode_t mode, int flags,
                         const char* workingDir);
int regularfile_futimesat(RegularFile* dir, const char* pathname, const struct timeval times[2],
                          const char* workingDir);
int regularfile_utimensat(RegularFile* dir, const char* pathname, const struct timespec times[2],
                          int flags, const char* workingDir);
int regularfile_faccessat(RegularFile* dir, const char* pathname, int mode, int flags,
                          const char* workingDir);
int regularfile_mkdirat(RegularFile* dir, const char* pathname, mode_t mode,
                        const char* workingDir);
int regularfile_mknodat(RegularFile* dir, const char* pathname, mode_t mode, dev_t dev,
                        const char* workingDir);
int regularfile_linkat(RegularFile* olddir, const char* oldpath, RegularFile* newdir,
                       const char* newpath, int flags, const char* workingDir);
int regularfile_unlinkat(RegularFile* dir, const char* pathname, int flags, const char* workingDir);
int regularfile_symlinkat(RegularFile* dir, const char* linkpath, const char* target,
                          const char* workingDir);
ssize_t regularfile_readlinkat(RegularFile* dir, const char* pathname, char* buf, size_t bufsize,
                               const char* workingDir);
int regularfile_renameat2(RegularFile* olddir, const char* oldpath, RegularFile* newdir,
                          const char* newpath, unsigned int flags, const char* workingDir);
#ifdef SYS_statx
int regularfile_statx(RegularFile* dir, const char* pathname, int flags, unsigned int mask,
                      struct statx* statxbuf, const char* workingDir);
#endif

#endif /* SRC_MAIN_HOST_DESCRIPTOR_FILE_H_ */
