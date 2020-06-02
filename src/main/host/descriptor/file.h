/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_MAIN_HOST_DESCRIPTOR_FILE_H_
#define SRC_MAIN_HOST_DESCRIPTOR_FILE_H_

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>

/* Opaque type representing a file-backed file descriptor. */
typedef struct _File File;

/* free this with descriptor_unref() */
File* file_new(int handle);

/* Close the file with descriptor_close() */
int file_open(File* file, File* dir, const char* pathname, int flags, mode_t mode);
ssize_t file_read(File* file, void* buf, size_t bufSize);
ssize_t file_write(File* file, const void* buf, size_t bufSize);
int file_fstat(File* file, struct stat* statbuf);
int file_fstatfs(File* file, struct statfs* statbuf);
int file_fsync(File* file);
int file_fchown(File* file, uid_t owner, gid_t group);
int file_fchmod(File* file, mode_t mode);
int file_fchdir(File* file);
int file_ftruncate(File* file, off_t length);
int file_fallocate(File* file, int mode, off_t offset, off_t length);
int file_fadvise(File* file, off_t offset, off_t len, int advice);
int file_flock(File* file, int operation);
int file_fsetxattr(File* file, const char* name, const void* value, size_t size, int flags);
ssize_t file_fgetxattr(File* file, const char* name, void* value, size_t size);
ssize_t file_flistxattr(File* file, char* list, size_t size);
int file_fremovexattr(File* file, const char* name);

int file_fstatat(File* file, const char* pathname, struct stat* statbuf, int flags);

/* Return the OS-backed file descriptor we use to operate on the file. */
int file_getOSBackedFD(File* file);

#endif /* SRC_MAIN_HOST_DESCRIPTOR_FILE_H_ */
