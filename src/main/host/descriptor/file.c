/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/file.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
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

int file_getOSBackedFD(File* file) {
    MAGIC_ASSERT(file);
    return file->osBackedFD;
}

int file_open(File* file, File* dir, const char* pathname, int flags, mode_t mode) {
    MAGIC_ASSERT(file);

    /* TODO: we should open the os-backed file in non-blocking mode even if a
     * non-block is not requested, and then properly handle the io by, e.g.,
     * epolling on all such files with a shadow support thread. */
    int dirfd = dir ? file_getOSBackedFD(dir) : AT_FDCWD;
    int osfd = openat(dirfd, pathname, flags, mode);

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

ssize_t file_read(File* file, void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i reading %zu bytes from os-backed file %i", descriptor_getHandle(&file->super), bufSize, file->osBackedFD);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = read(file->osBackedFD, buf, bufSize);
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

int file_fstatat(File* file, const char* pathname, struct stat* statbuf, int flags) {
    MAGIC_ASSERT(file);

    if(!file->osBackedFD) {
        return -EBADF;
    }

    debug("File %i fstatat os-backed file %i", descriptor_getHandle(&file->super), file->osBackedFD);

    int result = fstatat(file->osBackedFD, pathname, statbuf, flags);
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

