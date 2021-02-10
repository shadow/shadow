/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/xattr.h>
#include <syscall.h>
#include <unistd.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/host.h"
#include "main/host/syscall/kernel_types.h"
#include "main/routing/dns.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

typedef enum _FileType FileType;
enum _FileType {
    FILE_TYPE_NOTSET,
    FILE_TYPE_REGULAR,
    FILE_TYPE_TEMP,
    FILE_TYPE_RANDOM,    // special handling for /dev/random etc.
    FILE_TYPE_HOSTS,     // special handling for /etc/hosts
    FILE_TYPE_LOCALTIME, // TODO: special handling for /etc/localtime
};

struct _File {
    /* File is a sub-type of a descriptor. */
    LegacyDescriptor super;
    FileType type;
    /* Info related to our OS-backed file. */
    struct {
        int fd;
        int flags;
        mode_t mode;
        char* abspath;
    } osfile;
    MAGIC_DECLARE;
};

int file_getFlags(File* file) {
    MAGIC_ASSERT(file);
    return file->osfile.flags;
}

mode_t file_getMode(File* file) {
    MAGIC_ASSERT(file);
    return file->osfile.mode;
}

char* file_getAbsolutePath(File* file) {
    MAGIC_ASSERT(file);
    return file->osfile.abspath;
}

static inline File* _file_descriptorToFile(LegacyDescriptor* desc) {
    utility_assert(descriptor_getType(desc) == DT_FILE);
    File* file = (File*)desc;
    MAGIC_ASSERT(file);
    return file;
}

static inline int _file_getFD(File* file) {
    MAGIC_ASSERT(file);
    return descriptor_getHandle(&file->super);
}

static inline int _file_getOSBackedFD(File* file) {
    MAGIC_ASSERT(file);
    return file->osfile.fd;
}
int file_getOSBackedFD(File* file) { return _file_getOSBackedFD(file); }

static void _file_closeHelper(File* file) {
    if (file && file->osfile.fd) {
        debug("On file %i, closing os-backed file %i", _file_getFD(file),
              _file_getOSBackedFD(file));

        close(file->osfile.fd);
        file->osfile.fd = 0;

        if (file->type == FILE_TYPE_TEMP && file->osfile.abspath) {
            if (unlink(file->osfile.abspath) < 0) {
                info("unlink unable to remove temporary file at '%s', error "
                     "%i: %s",
                     file->osfile.abspath, errno, strerror(errno));
            }
        }

        /* The os-backed file is no longer ready. */
        descriptor_adjustStatus(&file->super, STATUS_DESCRIPTOR_ACTIVE, FALSE);
    }
}

static gboolean _file_close(LegacyDescriptor* desc) {
    File* file = _file_descriptorToFile(desc);

    debug("Closing file %i with os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    /* Make sure we mimic the close on the OS-backed file now. */
    _file_closeHelper(file);

    /* tell the host to stop tracking us, and unref the descriptor.
     * this should trigger _file_free in most cases. */
    return TRUE;
}

static void _file_free(LegacyDescriptor* desc) {
    File* file = _file_descriptorToFile(desc);

    debug("Freeing file %i with os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    _file_closeHelper(file);

    if (file->osfile.abspath) {
        free(file->osfile.abspath);
    }

    descriptor_clear((LegacyDescriptor*)file);
    MAGIC_CLEAR(file);
    free(file);

    worker_countObject(OBJECT_TYPE_FILE, COUNTER_TYPE_FREE);
}

static DescriptorFunctionTable _fileFunctions = (DescriptorFunctionTable){
    .close = _file_close,
    .free = _file_free,
};

File* file_new() {
    File* file = malloc(sizeof(File));

    *file = (File){0};

    descriptor_init(&(file->super), DT_FILE, &_fileFunctions);
    MAGIC_INIT(file);

    worker_countObject(OBJECT_TYPE_FILE, COUNTER_TYPE_NEW);
    return file;
}

static char* _file_getConcatStr(const char* prefix, const char* suffix) {
    char* path = NULL;
    if (asprintf(&path, "%s/%s", prefix, suffix) < 0) {
        error("asprintf could not allocate a buffer, error %i: %s", errno,
              strerror(errno));
        abort();
    }
    return path;
}

static char* _file_getPath(File* file, File* dir, const char* pathname) {
    MAGIC_ASSERT(file);
    utility_assert(pathname);

    /* Compute the absolute path, which will allow us to reopen later. */
    if (pathname[0] == '/') {
        /* The path is already absolute. Just copy it. */
        return strdup(pathname);
    }

    /* The path is relative, try dir prefix first. */
    if (dir && dir->osfile.abspath) {
        return _file_getConcatStr(dir->osfile.abspath, pathname);
    }

    /* Use current working directory as prefix. */
    char* cwd = getcwd(NULL, 0);
    if (!cwd) {
        error("getcwd unable to allocate string buffer, error %i: %s", errno,
              strerror(errno));
        abort();
    }

    char* abspath = _file_getConcatStr(cwd, pathname);
    free(cwd);
    return abspath;
}

static char* _file_getTempPathTemplate(File* file, const char* pathname) {
    char* abspath = NULL;
    if (asprintf(&abspath, "%s/shadow-%i-tmpfd-%d-XXXXXX", pathname,
                 (int)getpid(), _file_getFD(file)) < 0) {
        error("asprintf could not allocate string for temp file, error %i: %s",
              errno, strerror(errno));
        abort();
    }
    return abspath;
}

int file_openat(File* file, File* dir, const char* pathname, int flags,
                mode_t mode) {
    MAGIC_ASSERT(file);
    utility_assert(file->osfile.fd == 0);

    /* TODO: we should open the os-backed file in non-blocking mode even if a
     * non-block is not requested, and then properly handle the io by, e.g.,
     * epolling on all such files with a shadow support thread. */
    int osfd = 0;
    char* abspath = NULL;

    debug("Attempting to open file with pathname '%s'", pathname);

    if (flags & O_TMPFILE) {
        /* We need to store a copy of the temp path so we can reopen it. */
        file->type = FILE_TYPE_TEMP;
        abspath = _file_getTempPathTemplate(file, pathname);
        osfd = mkostemp(abspath, flags & ~O_TMPFILE);
    } else {
        /* The default case is a regular file. We do this first so that we have
         * an absolute path to compare for special files. */
        abspath = _file_getPath(file, dir, pathname);

        /* TODO: Handle special files. */
        if (utility_isRandomPath(abspath)) {
            file->type = FILE_TYPE_RANDOM;
        } else if (!strcmp("/etc/hosts", abspath)) {
            file->type = FILE_TYPE_HOSTS;
            char* hostspath = dns_getHostsFilePath(worker_getDNS());
            if(hostspath && abspath) {
                free(abspath);
                abspath = hostspath;
            }
        } else if (!strcmp("/etc/localtime", abspath)) {
            file->type = FILE_TYPE_LOCALTIME;
        } else {
            file->type = FILE_TYPE_REGULAR;
        }

        if(file->type == FILE_TYPE_LOCALTIME) {
            // Fail the localtime lookup so the plugin falls back to UTC.
            // TODO: we could instead return a special file that contains
            // timezone info in the correct format for UTC.
            osfd = -1;
            errno = ENOENT;
        } else {
            osfd = open(abspath, flags, mode);
        }
    }

    if (osfd < 0) {
        debug("File %i opening path '%s' returned %i: %s", _file_getFD(file),
              abspath, osfd, strerror(errno));
        if (abspath) {
            free(abspath);
        }
        file->type = FILE_TYPE_NOTSET;
        return -errno;
    }

    /* Store the create information so we can open later if needed. */
    file->osfile.fd = osfd;
    file->osfile.abspath = abspath;
    /* We can't use O_TMPFILE, because if we reopen, we'll get a new file. */
    file->osfile.flags = (flags & ~O_TMPFILE);
    file->osfile.mode = mode;

    debug("File %i opened os-backed file %i at absolute path %s",
          _file_getFD(file), _file_getOSBackedFD(file), file->osfile.abspath);

    /* The os-backed file is now ready. */
    descriptor_adjustStatus(&file->super, STATUS_DESCRIPTOR_ACTIVE, TRUE);

    return _file_getFD(file);
}

int file_open(File* file, const char* pathname, int flags, mode_t mode) {
    return file_openat(file, NULL, pathname, flags, mode);
}

static void _file_readRandomBytes(File* file, void* buf, size_t numBytes) {
    utility_assert(file->type == FILE_TYPE_RANDOM);

    Host* host = worker_getActiveHost();
    utility_assert(host != NULL);

    debug("File %i will read %zu bytes from random source for host %s", _file_getFD(file), numBytes,
          host_getName(host));

    Random* rng = host_getRandom(host);
    random_nextNBytes(rng, buf, numBytes);
}

static size_t _file_readvRandomBytes(File* file, const struct iovec* iov, int iovcnt) {
    size_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        _file_readRandomBytes(file, iov[i].iov_base, iov[i].iov_len);
        total += iov[i].iov_len;
    }
    return total;
}

ssize_t file_read(File* file, void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        _file_readRandomBytes(file, buf, bufSize);
        return (ssize_t)bufSize;
    }

    debug("File %i will read %zu bytes from os-backed file %i at path '%s'",
          _file_getFD(file), bufSize, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = read(_file_getOSBackedFD(file), buf, bufSize);
    return (result < 0) ? -errno : result;
}

ssize_t file_pread(File* file, void* buf, size_t bufSize, off_t offset) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        _file_readRandomBytes(file, buf, bufSize);
        return (ssize_t)bufSize;
    }

    debug("File %i will pread %zu bytes from os-backed file %i at path '%s'",
          _file_getFD(file), bufSize, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pread(_file_getOSBackedFD(file), buf, bufSize, offset);
    return (result < 0) ? -errno : result;
}

ssize_t file_preadv(File* file, const struct iovec* iov, int iovcnt, off_t offset) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        return (ssize_t)_file_readvRandomBytes(file, iov, iovcnt);
    }

    debug("File %i will preadv %d vector items from os-backed file %i at path "
          "'%s'",
          _file_getFD(file), iovcnt, _file_getOSBackedFD(file), file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = preadv(_file_getOSBackedFD(file), iov, iovcnt, offset);
    return (result < 0) ? -errno : result;
}

#ifdef SYS_preadv2
ssize_t file_preadv2(File* file, const struct iovec* iov, int iovcnt,
                     off_t offset, int flags) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        return (ssize_t)_file_readvRandomBytes(file, iov, iovcnt);
    }

    debug("File %i will preadv2 %d vector items from os-backed file %i at path "
          "'%s'",
          _file_getFD(file), iovcnt, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result =
        preadv2(_file_getOSBackedFD(file), iov, iovcnt, offset, flags);
    return (result < 0) ? -errno : result;
}
#endif

ssize_t file_write(File* file, const void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i will write %zu bytes to os-backed file %i at path '%s'",
          _file_getFD(file), bufSize, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = write(_file_getOSBackedFD(file), buf, bufSize);
    return (result < 0) ? -errno : result;
}

ssize_t file_pwrite(File* file, const void* buf, size_t bufSize, off_t offset) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i will pwrite %zu bytes to os-backed file %i at path '%s'",
          _file_getFD(file), bufSize, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pwrite(_file_getOSBackedFD(file), buf, bufSize, offset);
    return (result < 0) ? -errno : result;
}

ssize_t file_pwritev(File* file, const struct iovec* iov, int iovcnt, off_t offset) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i will pwritev %d vector items from os-backed file %i at "
          "path '%s'",
          _file_getFD(file), iovcnt, _file_getOSBackedFD(file), file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pwritev(_file_getOSBackedFD(file), iov, iovcnt, offset);
    return (result < 0) ? -errno : result;
}

#ifdef SYS_pwritev2
ssize_t file_pwritev2(File* file, const struct iovec* iov, int iovcnt,
                      off_t offset, int flags) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i will pwritev2 %d vector items from os-backed file %i at "
          "path '%s'",
          _file_getFD(file), iovcnt, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result =
        pwritev2(_file_getOSBackedFD(file), iov, iovcnt, offset, flags);
    return (result < 0) ? -errno : result;
}
#endif

int file_fstat(File* file, struct stat* statbuf) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fstat os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fstat(_file_getOSBackedFD(file), statbuf);
    return (result < 0) ? -errno : result;
}

int file_fstatfs(File* file, struct statfs* statbuf) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fstatfs os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fstatfs(_file_getOSBackedFD(file), statbuf);
    return (result < 0) ? -errno : result;
}

int file_fsync(File* file) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fsync os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fsync(_file_getOSBackedFD(file));
    return (result < 0) ? -errno : result;
}

int file_fchown(File* file, uid_t owner, gid_t group) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fchown os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fchown(_file_getOSBackedFD(file), owner, group);
    return (result < 0) ? -errno : result;
}

int file_fchmod(File* file, mode_t mode) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fchmod os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fchmod(_file_getOSBackedFD(file), mode);
    return (result < 0) ? -errno : result;
}

int file_fchdir(File* file) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fchdir os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fchdir(_file_getOSBackedFD(file));
    return (result < 0) ? -errno : result;
}

int file_ftruncate(File* file, off_t length) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i ftruncate os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = ftruncate(_file_getOSBackedFD(file), length);
    return (result < 0) ? -errno : result;
}

int file_fallocate(File* file, int mode, off_t offset, off_t length) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fallocate os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fallocate(_file_getOSBackedFD(file), mode, offset, length);
    return (result < 0) ? -errno : result;
}

int file_fadvise(File* file, off_t offset, off_t len, int advice) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fadvise os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = posix_fadvise(_file_getOSBackedFD(file), offset, len, advice);
    return (result < 0) ? -errno : result;
}

int file_flock(File* file, int operation) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i flock os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = flock(_file_getOSBackedFD(file), operation);
    return (result < 0) ? -errno : result;
}

int file_fsetxattr(File* file, const char* name, const void* value, size_t size,
                   int flags) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fsetxattr os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fsetxattr(_file_getOSBackedFD(file), name, value, size, flags);
    return (result < 0) ? -errno : result;
}

ssize_t file_fgetxattr(File* file, const char* name, void* value, size_t size) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fgetxattr os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = fgetxattr(_file_getOSBackedFD(file), name, value, size);
    return (result < 0) ? -errno : result;
}

ssize_t file_flistxattr(File* file, char* list, size_t size) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i flistxattr os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = flistxattr(_file_getOSBackedFD(file), list, size);
    return (result < 0) ? -errno : result;
}

int file_fremovexattr(File* file, const char* name) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fremovexattr os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fremovexattr(_file_getOSBackedFD(file), name);
    return (result < 0) ? -errno : result;
}

int file_sync_range(File* file, off64_t offset, off64_t nbytes,
                    unsigned int flags) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i sync_file_range os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result =
        sync_file_range(_file_getOSBackedFD(file), offset, nbytes, flags);
    return (result < 0) ? -errno : result;
}

ssize_t file_readahead(File* file, off64_t offset, size_t count) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i readahead os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = readahead(_file_getOSBackedFD(file), offset, count);
    return (result < 0) ? -errno : result;
}

off_t file_lseek(File* file, off_t offset, int whence) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i lseek os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = lseek(_file_getOSBackedFD(file), offset, whence);
    return (result < 0) ? -errno : result;
}

int file_getdents(File* file, struct linux_dirent* dirp, unsigned int count) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i getdents os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    // getdents is not available for a direct call
    int result =
        (int)syscall(SYS_getdents, _file_getOSBackedFD(file), dirp, count);
    return (result < 0) ? -errno : result;
}

int file_getdents64(File* file, struct linux_dirent64* dirp,
                    unsigned int count) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i getdents64 os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result =
        (int)syscall(SYS_getdents64, _file_getOSBackedFD(file), dirp, count);
    return (result < 0) ? -errno : result;
}

int file_ioctl(File* file, unsigned long request, void* arg) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i ioctl os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = ioctl(_file_getOSBackedFD(file), request, arg);
    return (result < 0) ? -errno : result;
}

int file_fcntl(File* file, unsigned long command, void* arg) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    debug("File %i fcntl os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fcntl(_file_getOSBackedFD(file), command, arg);
    return (result < 0) ? -errno : result;
}

///////////////////////////////////////////////
// *at functions (NULL directory file is valid)
///////////////////////////////////////////////

static inline int _file_getOSDirFD(File* dir) {
    if (dir) {
        MAGIC_ASSERT(dir);
        return dir->osfile.fd > 0 ? dir->osfile.fd : AT_FDCWD;
    } else {
        return AT_FDCWD;
    }
}

int file_fstatat(File* dir, const char* pathname, struct stat* statbuf,
                 int flags) {
    debug("File %i fstatat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = fstatat(_file_getOSDirFD(dir), pathname, statbuf, flags);
    return (result < 0) ? -errno : result;
}

int file_fchownat(File* dir, const char* pathname, uid_t owner, gid_t group,
                  int flags) {
    debug("File %i fchownat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = fchownat(_file_getOSDirFD(dir), pathname, owner, group, flags);
    return (result < 0) ? -errno : result;
}

int file_fchmodat(File* dir, const char* pathname, mode_t mode, int flags) {
    debug("File %i fchmodat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = fchmodat(_file_getOSDirFD(dir), pathname, mode, flags);
    return (result < 0) ? -errno : result;
}

int file_futimesat(File* dir, const char* pathname,
                   const struct timeval times[2]) {
    debug("File %i futimesat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = futimesat(_file_getOSDirFD(dir), pathname, times);
    return (result < 0) ? -errno : result;
}

int file_utimensat(File* dir, const char* pathname,
                   const struct timespec times[2], int flags) {
    debug("File %i utimesat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = utimensat(_file_getOSDirFD(dir), pathname, times, flags);
    return (result < 0) ? -errno : result;
}

int file_faccessat(File* dir, const char* pathname, int mode, int flags) {
    debug("File %i faccessat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = faccessat(_file_getOSDirFD(dir), pathname, mode, flags);
    return (result < 0) ? -errno : result;
}

int file_mkdirat(File* dir, const char* pathname, mode_t mode) {
    debug("File %i mkdirat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = mkdirat(_file_getOSDirFD(dir), pathname, mode);
    return (result < 0) ? -errno : result;
}

int file_mknodat(File* dir, const char* pathname, mode_t mode, dev_t dev) {
    debug("File %i mknodat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = mknodat(_file_getOSDirFD(dir), pathname, mode, dev);
    return (result < 0) ? -errno : result;
}

int file_linkat(File* olddir, const char* oldpath, File* newdir,
                const char* newpath, int flags) {
    int oldosdirfd = _file_getOSDirFD(olddir);
    int newosdirfd = _file_getOSDirFD(newdir);

    debug("File %i linkat os-backed file %i", olddir ? _file_getFD(olddir) : 0,
          oldosdirfd);

    int result = linkat(oldosdirfd, oldpath, newosdirfd, newpath, flags);
    return (result < 0) ? -errno : result;
}

int file_unlinkat(File* dir, const char* pathname, int flags) {
    debug("File %i unlinkat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = unlinkat(_file_getOSDirFD(dir), pathname, flags);
    return (result < 0) ? -errno : result;
}

int file_symlinkat(File* dir, const char* linkpath, const char* target) {
    debug("File %i symlinkat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = symlinkat(target, _file_getOSDirFD(dir), linkpath);
    return (result < 0) ? -errno : result;
}

ssize_t file_readlinkat(File* dir, const char* pathname, char* buf,
                        size_t bufsize) {
    debug("File %i readlinkat os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    ssize_t result = readlinkat(_file_getOSDirFD(dir), pathname, buf, bufsize);
    return (result < 0) ? -errno : result;
}

int file_renameat2(File* olddir, const char* oldpath, File* newdir,
                   const char* newpath, unsigned int flags) {
    int oldosdirfd = _file_getOSDirFD(olddir);
    int newosdirfd = _file_getOSDirFD(newdir);

    debug("File %i renameat2 os-backed file %i",
          olddir ? _file_getFD(olddir) : 0, oldosdirfd);

    int result = (int)syscall(
        SYS_renameat2, oldosdirfd, oldpath, newosdirfd, newpath, flags);
    return (result < 0) ? -errno : result;
}

#ifdef SYS_statx
int file_statx(File* dir, const char* pathname, int flags, unsigned int mask,
               struct statx* statxbuf) {
    debug("File %i statx os-backed file %i", dir ? _file_getFD(dir) : 0,
          _file_getOSDirFD(dir));

    int result = (int)syscall(
        SYS_statx, _file_getOSBackedFD(dir), pathname, flags, mask, statxbuf);
    return (result < 0) ? -errno : result;
}
#endif
