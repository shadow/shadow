/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/file.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <syscall.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/syscall/dirent.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

typedef enum _FileType FileType;
enum _FileType {
    FILE_TYPE_NOTSET,
    FILE_TYPE_FILE,
    FILE_TYPE_RANDOM, // TODO: special handling for /dev/random etc.
    FILE_TYPE_HOSTS, // TODO: special handling for /etc/hosts
    FILE_TYPE_LOCALTIME, // TODO: special handling for /etc/localtime
};

struct _File {
    /* File is a sub-type of a descriptor. */
    Descriptor super;
    FileType type;
    int osBackedFD;
    MAGIC_DECLARE;
};

static File* _file_descriptorToFile(Descriptor* desc) {
    utility_assert(descriptor_getType(desc) == DT_FILE);
    File* f = (File*)desc;
    MAGIC_ASSERT(f);
    return f;
}

static void _file_closeHelper(Descriptor* desc) {
    File* file = _file_descriptorToFile(desc);
    if(file->osBackedFD) {
        close(file->osBackedFD);
        file->osBackedFD = 0;
    }
}

static void _file_close(Descriptor* desc) {
    /* tell the host to stop tracking us, and unref the descriptor.
     * this should trigger _file_free in most cases. */
    host_closeDescriptor(worker_getActiveHost(), descriptor_getHandle(desc));
}

static void _file_free(Descriptor* desc) {
    File* file = _file_descriptorToFile(desc);

    _file_closeHelper(desc);

    MAGIC_CLEAR(file);
    free(file);

    worker_countObject(OBJECT_TYPE_FILE, COUNTER_TYPE_FREE);
}

static DescriptorFunctionTable _fileFunctions = (DescriptorFunctionTable){
    .close = _file_close,
    .free = _file_free,
};

File* file_new(int handle) {
    File* file = malloc(sizeof(File));

    *file = (File){0};

    descriptor_init(&(file->super), DT_FILE, &_fileFunctions, handle);
    MAGIC_INIT(file);

    worker_countObject(OBJECT_TYPE_FILE, COUNTER_TYPE_NEW);
    return file;
}

static int _file_getOSBackedFD(File* file) {
    MAGIC_ASSERT(file);
    return file->osBackedFD;
}

int file_openat(File* file, File* dir, const char* pathname, int flags, mode_t mode) {
    MAGIC_ASSERT(file);

    /* TODO: we should open the os-backed file in non-blocking mode even if a
     * non-block is not requested, and then properly handle the io by, e.g.,
     * epolling on all such files with a shadow support thread. */
    int osfd = 0;
    if(dir) {
        osfd = openat(_file_getOSBackedFD(dir), pathname, flags, mode);
    } else {
        osfd = open(pathname, flags, mode);
    }

    if(osfd < 0) {
        return -errno;
    }

    if(pathname) {
        /* TODO handle special file types. */
        if(utility_isRandomPath(pathname)) {
            file->type = FILE_TYPE_RANDOM;
        } else if(!strncmp("/etc/hosts", pathname, 10)) {
            file->type = FILE_TYPE_HOSTS;
        } else if(!strncmp("/etc/localtime", pathname, 14)) {
            file->type = FILE_TYPE_LOCALTIME;
        } else {
            file->type = FILE_TYPE_FILE;
        }
    }

    file->osBackedFD = osfd;

    debug("File %i opened os-backed file %i at path %s", descriptor_getHandle(&file->super), file->osBackedFD, pathname ? pathname : "NULL");

    return file->super.handle;
}

int file_open(File* file, const char* pathname, int flags, mode_t mode) {
    return file_openat(file, NULL, pathname, flags, mode);
}

ssize_t file_read(File* file, void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i read %zu bytes from os-backed file %i", descriptor_getHandle(&file->super), bufSize, file->osBackedFD);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = read(file->osBackedFD, buf, bufSize);
    return (result < 0) ? -errno : result;
}

ssize_t file_pread(File* file, void* buf, size_t bufSize, off_t offset) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i pread %zu bytes from os-backed file %i", descriptor_getHandle(&file->super), bufSize, file->osBackedFD);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pread(file->osBackedFD, buf, bufSize, offset);
    return (result < 0) ? -errno : result;
}

ssize_t file_write(File* file, const void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i writing %zu bytes to os-backed file %i", descriptor_getHandle(&file->super), bufSize, file->osBackedFD);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = write(file->osBackedFD, buf, bufSize);
    return (result < 0) ? -errno : result;
}

ssize_t file_pwrite(File* file, const void* buf, size_t bufSize, off_t offset) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i writing %zu bytes to os-backed file %i", descriptor_getHandle(&file->super), bufSize, file->osBackedFD);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pwrite(file->osBackedFD, buf, bufSize, offset);
    return (result < 0) ? -errno : result;
}

int file_fstat(File* file, struct stat* statbuf) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fstat os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fstat(file->osBackedFD, statbuf);
    return (result < 0) ? -errno : result;
}

int file_fstatfs(File* file, struct statfs* statbuf) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fstatfs os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fstatfs(file->osBackedFD, statbuf);
    return (result < 0) ? -errno : result;
}

int file_fsync(File* file) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fsync os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fsync(file->osBackedFD);
    return (result < 0) ? -errno : result;
}

int file_fchown(File* file, uid_t owner, gid_t group) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fchown os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fchown(file->osBackedFD, owner, group);
    return (result < 0) ? -errno : result;
}

int file_fchmod(File* file, mode_t mode) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fchmod os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fchmod(file->osBackedFD, mode);
    return (result < 0) ? -errno : result;
}

int file_fchdir(File* file) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fchdir os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fchdir(file->osBackedFD);
    return (result < 0) ? -errno : result;
}

int file_ftruncate(File* file, off_t length) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i ftruncate os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = ftruncate(file->osBackedFD, length);
    return (result < 0) ? -errno : result;
}

int file_fallocate(File* file, int mode, off_t offset, off_t length) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fallocate os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fallocate(file->osBackedFD, mode, offset, length);
    return (result < 0) ? -errno : result;
}

int file_fadvise(File* file, off_t offset, off_t len, int advice) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fadvise os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = posix_fadvise(file->osBackedFD, offset, len, advice);
    return (result < 0) ? -errno : result;
}

int file_flock(File* file, int operation) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i flock os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = flock(file->osBackedFD, operation);
    return (result < 0) ? -errno : result;
}

int file_fsetxattr(File* file, const char* name, const void* value, size_t size, int flags) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fsetxattr os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fsetxattr(file->osBackedFD, name, value, size, flags);
    return (result < 0) ? -errno : result;
}

ssize_t file_fgetxattr(File* file, const char* name, void* value, size_t size) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fgetxattr os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    ssize_t result = fgetxattr(file->osBackedFD, name, value, size);
    return (result < 0) ? -errno : result;
}

ssize_t file_flistxattr(File* file, char* list, size_t size) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i flistxattr os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    ssize_t result = flistxattr(file->osBackedFD, list, size);
    return (result < 0) ? -errno : result;
}

int file_fremovexattr(File* file, const char* name) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fremovexattr os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fremovexattr(file->osBackedFD, name);
    return (result < 0) ? -errno : result;
}

int file_sync_range(File* file, off64_t offset, off64_t nbytes, unsigned int flags) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i sync_file_range os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = sync_file_range(file->osBackedFD, offset, nbytes, flags);
    return (result < 0) ? -errno : result;
}

ssize_t file_readahead(File* file, off64_t offset, size_t count) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i readahead os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    ssize_t result = readahead(file->osBackedFD, offset, count);
    return (result < 0) ? -errno : result;
}

off_t file_lseek(File* file, off_t offset, int whence) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i lseek os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    ssize_t result = lseek(file->osBackedFD, offset, whence);
    return (result < 0) ? -errno : result;
}

int file_getdents(File* file, struct linux_dirent* dirp, unsigned int count) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i getdents os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    // getdents is not available for a direct call
    int result = (int)syscall(SYS_getdents, file->osBackedFD, dirp, count);
    return (result < 0) ? -errno : result;
}

int file_getdents64(File* file, struct linux_dirent64* dirp, unsigned int count) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i getdents64 os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = getdents64(file->osBackedFD, dirp, count);
    return (result < 0) ? -errno : result;
}

///////////////////////////////////////////////
// *at functions (NULL directory file is valid)
///////////////////////////////////////////////

static inline int _file_getOSDirFDHelper(File* dir) {
    if(dir) {
        MAGIC_ASSERT(dir);
        return dir->osBackedFD > 0 ? dir->osBackedFD : AT_FDCWD;
    } else {
        return AT_FDCWD;
    }
}

int file_fstatat(File* dir, const char* pathname, struct stat* statbuf, int flags) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i fstatat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = fstatat(osdirfd, pathname, statbuf, flags);
    return (result < 0) ? -errno : result;
}

int file_fchownat(File* dir, const char* pathname, uid_t owner, gid_t group, int flags) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i fchownat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = fchownat(osdirfd, pathname, owner, group, flags);
    return (result < 0) ? -errno : result;
}

int file_fchmodat(File* dir, const char* pathname, mode_t mode, int flags) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i fchmodat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = fchmodat(osdirfd, pathname, mode, flags);
    return (result < 0) ? -errno : result;
}

int file_futimesat(File* dir, const char* pathname, const struct timeval times[2]) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i futimesat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = futimesat(osdirfd, pathname, times);
    return (result < 0) ? -errno : result;
}

int file_utimensat(File* dir, const char* pathname, const struct timespec times[2], int flags) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i utimesat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = utimensat(osdirfd, pathname, times, flags);
    return (result < 0) ? -errno : result;
}

int file_faccessat(File* dir, const char* pathname, int mode, int flags) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i faccessat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = faccessat(osdirfd, pathname, mode, flags);
    return (result < 0) ? -errno : result;
}

int file_mkdirat(File* dir, const char* pathname, mode_t mode) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i mkdirat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = mkdirat(osdirfd, pathname, mode);
    return (result < 0) ? -errno : result;
}

int file_mknodat(File* dir, const char* pathname, mode_t mode, dev_t dev) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i mknodat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = mknodat(osdirfd, pathname, mode, dev);
    return (result < 0) ? -errno : result;
}

int file_linkat(File* olddir, const char* oldpath, File* newdir, const char* newpath, int flags) {
    int oldosdirfd = _file_getOSDirFDHelper(olddir);
    int newosdirfd = _file_getOSDirFDHelper(newdir);

    debug("File %i linkat os-backed file %i", olddir ? descriptor_getHandle(&olddir->super) : 0, oldosdirfd);

    int result = linkat(oldosdirfd, oldpath, newosdirfd, newpath, flags);
    return (result < 0) ? -errno : result;
}

int file_unlinkat(File* dir, const char* pathname, int flags) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i unlinkat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = unlinkat(osdirfd, pathname, flags);
    return (result < 0) ? -errno : result;
}

int file_symlinkat(File* dir, const char* linkpath, const char* target) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i symlinkat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = symlinkat(target, osdirfd, linkpath);
    return (result < 0) ? -errno : result;
}

ssize_t file_readlinkat(File* dir, const char* pathname, char* buf, size_t bufsize) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i readlinkat os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    ssize_t result = readlinkat(osdirfd, pathname, buf, bufsize);
    return (result < 0) ? -errno : result;
}

int file_renameat2(File* olddir, const char* oldpath, File* newdir, const char* newpath, unsigned int flags) {
    int oldosdirfd = _file_getOSDirFDHelper(olddir);
    int newosdirfd = _file_getOSDirFDHelper(newdir);

    debug("File %i renameat2 os-backed file %i", olddir ? descriptor_getHandle(&olddir->super) : 0, oldosdirfd);

    int result = renameat2(oldosdirfd, oldpath, newosdirfd, newpath, flags);
    return (result < 0) ? -errno : result;
}

int file_statx(File* dir, const char* pathname, int flags, unsigned int mask, struct statx* statxbuf) {
    int osdirfd = _file_getOSDirFDHelper(dir);

    debug("File %i statx os-backed file %i", dir ? descriptor_getHandle(&dir->super) : 0, osdirfd);

    int result = statx(osdirfd, pathname, flags, mask, statxbuf);
    return (result < 0) ? -errno : result;
}
