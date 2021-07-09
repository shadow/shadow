/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/file.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
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

#include "lib/logger/logger.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/host.h"
#include "main/host/syscall/kernel_types.h"
#include "main/routing/dns.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"

#define OSFILE_INVALID -1

typedef enum _FileType FileType;
enum _FileType {
    FILE_TYPE_NOTSET,
    FILE_TYPE_REGULAR,
    FILE_TYPE_RANDOM,    // special handling for /dev/random etc.
    FILE_TYPE_HOSTS,     // special handling for /etc/hosts
    FILE_TYPE_LOCALTIME, // special handling for /etc/localtime
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
    if (file && file->osfile.fd != OSFILE_INVALID) {
        trace("On file %i, closing os-backed file %i", _file_getFD(file),
              _file_getOSBackedFD(file));

        close(file->osfile.fd);
        file->osfile.fd = OSFILE_INVALID;

        /* The os-backed file is no longer ready. */
        descriptor_adjustStatus(&file->super, STATUS_DESCRIPTOR_ACTIVE, FALSE);
    }
}

static gboolean _file_close(LegacyDescriptor* desc, Host* host) {
    File* file = _file_descriptorToFile(desc);

    trace("Closing file %i with os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    /* Make sure we mimic the close on the OS-backed file now. */
    _file_closeHelper(file);

    /* tell the host to stop tracking us, and unref the descriptor.
     * this should trigger _file_free in most cases. */
    return TRUE;
}

static void _file_free(LegacyDescriptor* desc) {
    File* file = _file_descriptorToFile(desc);

    trace("Freeing file %i with os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    _file_closeHelper(file);

    if (file->osfile.abspath) {
        free(file->osfile.abspath);
    }

    descriptor_clear((LegacyDescriptor*)file);
    MAGIC_CLEAR(file);
    free(file);

    worker_count_deallocation(File);
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
    file->osfile.fd = OSFILE_INVALID; // negative means uninitialized (0 is a valid fd)

    worker_count_allocation(File);
    return file;
}

File* file_dup(File* file, int* dupError) {
    MAGIC_ASSERT(file);

    int newFd;

    // only dup the os fd if it's valid
    if (file->osfile.fd >= 0) {
        newFd = dup(file->osfile.fd);

        if (newFd < 0) {
            *dupError = errno;
            return NULL;
        }
    } else {
        newFd = file->osfile.fd;
    }

    File* newFile = file_new();

    newFile->type = file->type;

    newFile->osfile.fd = newFd;
    newFile->osfile.flags = file->osfile.flags;
    newFile->osfile.mode = file->osfile.mode;
    newFile->osfile.abspath = strdup(file->osfile.abspath);

    // CLOEXEC is a descriptor flag and it is not copied during a dup()
    newFile->osfile.flags &= ~O_CLOEXEC;

    return newFile;
}

static char* _file_getConcatStr(const char* prefix, const char sep, const char* suffix) {
    char* path = NULL;
    if (asprintf(&path, "%s%c%s", prefix, sep, suffix) < 0) {
        utility_panic("asprintf could not allocate a buffer, error %i: %s", errno, strerror(errno));
        abort();
    }
    return path;
}

static char* _file_getAbsolutePath(File* dir, const char* pathname, const char* workingDir) {
    utility_assert(pathname);
    utility_assert(workingDir);
    utility_assert(workingDir[0] == '/');

    /* Compute the absolute path, which will allow us to reopen later. */
    if (pathname[0] == '/') {
        /* The path is already absolute. Just copy it. */
        return strdup(pathname);
    }

    /* The path is relative, try dir prefix first. */
    if (dir && dir->osfile.abspath) {
        return _file_getConcatStr(dir->osfile.abspath, '/', pathname);
    }

    /* Use current working directory as prefix. */
    char* abspath = _file_getConcatStr(workingDir, '/', pathname);
    return abspath;
}

#ifdef DEBUG
#define CHECK_FLAG(flag)                                                                           \
    if (flags & flag) {                                                                            \
        if (!flag_str) {                                                                           \
            asprintf(&flag_str, #flag);                                                            \
        } else {                                                                                   \
            char* str = _file_getConcatStr(flag_str, '|', #flag);                                  \
            free(flag_str);                                                                        \
            flag_str = str;                                                                        \
        }                                                                                          \
    }
static void _file_print_flags(int flags) {
    char* flag_str = NULL;
    CHECK_FLAG(O_APPEND);
    CHECK_FLAG(O_ASYNC);
    CHECK_FLAG(O_CLOEXEC);
    CHECK_FLAG(O_CREAT);
    CHECK_FLAG(O_DIRECT);
    CHECK_FLAG(O_DIRECTORY);
    CHECK_FLAG(O_DSYNC);
    CHECK_FLAG(O_EXCL);
    CHECK_FLAG(O_LARGEFILE);
    CHECK_FLAG(O_NOATIME);
    CHECK_FLAG(O_NOCTTY);
    CHECK_FLAG(O_NOFOLLOW);
    CHECK_FLAG(O_NONBLOCK);
    CHECK_FLAG(O_PATH);
    CHECK_FLAG(O_SYNC);
    CHECK_FLAG(O_TMPFILE);
    CHECK_FLAG(O_TRUNC);
    if (!flag_str) {
        asprintf(&flag_str, "0");
    }
    trace("Found flags: %s", flag_str);
    if (flag_str) {
        free(flag_str);
    }
}
#undef CHECK_FLAG
#endif

int file_openat(File* file, File* dir, const char* pathname, int flags, mode_t mode,
                const char* workingDir) {
    MAGIC_ASSERT(file);
    utility_assert(file->osfile.fd == OSFILE_INVALID);

    trace("Attempting to open file with pathname=%s flags=%i mode=%i workingdir=%s", pathname,
          flags, (int)mode, workingDir);
#ifdef DEBUG
    if (flags) {
        _file_print_flags(flags);
    }
#endif

    int fd = _file_getFD(file);
    if (fd < 0) {
        utility_panic("Cannot openat() on an unregistered descriptor object with fd %d", fd);
    }

    /* The default case is a regular file. We do this first so that we have
     * an absolute path to compare for special files. */
    char* abspath = _file_getAbsolutePath(dir, pathname, workingDir);

    /* Handle special files. */
    if (utility_isRandomPath(abspath)) {
        file->type = FILE_TYPE_RANDOM;
    } else if (!strcmp("/etc/hosts", abspath)) {
        file->type = FILE_TYPE_HOSTS;
        char* hostspath = dns_getHostsFilePath(worker_getDNS());
        if (hostspath && abspath) {
            free(abspath);
            abspath = hostspath;
        }
    } else if (!strcmp("/etc/localtime", abspath)) {
        file->type = FILE_TYPE_LOCALTIME;
    } else {
        file->type = FILE_TYPE_REGULAR;
    }

    int osfd = 0, errcode = 0;
    if (file->type == FILE_TYPE_LOCALTIME) {
        // Fail the localtime lookup so the plugin falls back to UTC.
        // TODO: we could instead return a special file that contains
        // timezone info in the correct format for UTC.
        osfd = -1;
        errcode = ENOENT;
    } else {
        // TODO: we should open the os-backed file in non-blocking mode even if a
        // non-block is not requested, and then properly handle the io by, e.g.,
        // epolling on all such files with a shadow support thread.
        osfd = open(abspath, flags, mode);
        errcode = errno;
    }

    if (osfd < 0) {
        trace("File %i opening path '%s' returned %i: %s", _file_getFD(file), abspath, osfd,
              strerror(errcode));
        if (abspath) {
            free(abspath);
        }
        file->type = FILE_TYPE_NOTSET;
        return -errcode;
    }

    /* Store the create information, which is used if we mmap the file later. */
    file->osfile.fd = osfd;
    file->osfile.abspath = abspath;
    file->osfile.flags = flags;
    file->osfile.mode = mode;

    trace("File %i opened os-backed file %i at absolute path %s",
          _file_getFD(file), _file_getOSBackedFD(file), file->osfile.abspath);

    /* The os-backed file is now ready. */
    descriptor_adjustStatus(&file->super, STATUS_DESCRIPTOR_ACTIVE, TRUE);

    /* We checked above that fd is non-negative. */
    return fd;
}

int file_open(File* file, const char* pathname, int flags, mode_t mode, const char* workingDir) {
    return file_openat(file, NULL, pathname, flags, mode, workingDir);
}

static void _file_readRandomBytes(File* file, Host* host, void* buf, size_t numBytes) {
    utility_assert(file->type == FILE_TYPE_RANDOM);

    utility_assert(host != NULL);

    trace("File %i will read %zu bytes from random source for host %s", _file_getFD(file), numBytes,
          host_getName(host));

    Random* rng = host_getRandom(host);
    random_nextNBytes(rng, buf, numBytes);
}

static size_t _file_readvRandomBytes(File* file, Host* host, const struct iovec* iov, int iovcnt) {
    size_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        _file_readRandomBytes(file, host, iov[i].iov_base, iov[i].iov_len);
        total += iov[i].iov_len;
    }
    return total;
}

ssize_t file_read(File* file, Host* host, void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        _file_readRandomBytes(file, host, buf, bufSize);
        return (ssize_t)bufSize;
    }

    trace("File %i will read %zu bytes from os-backed file %i at path '%s'",
          _file_getFD(file), bufSize, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = read(_file_getOSBackedFD(file), buf, bufSize);
    return (result < 0) ? -errno : result;
}

ssize_t file_pread(File* file, Host* host, void* buf, size_t bufSize, off_t offset) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        _file_readRandomBytes(file, host, buf, bufSize);
        return (ssize_t)bufSize;
    }

    trace("File %i will pread %zu bytes from os-backed file %i at path '%s'",
          _file_getFD(file), bufSize, _file_getOSBackedFD(file),
          file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pread(_file_getOSBackedFD(file), buf, bufSize, offset);
    return (result < 0) ? -errno : result;
}

ssize_t file_preadv(File* file, Host* host, const struct iovec* iov, int iovcnt, off_t offset) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        return (ssize_t)_file_readvRandomBytes(file, host, iov, iovcnt);
    }

    trace("File %i will preadv %d vector items from os-backed file %i at path "
          "'%s'",
          _file_getFD(file), iovcnt, _file_getOSBackedFD(file), file->osfile.abspath);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = preadv(_file_getOSBackedFD(file), iov, iovcnt, offset);
    return (result < 0) ? -errno : result;
}

#ifdef SYS_preadv2
ssize_t file_preadv2(File* file, Host* host, const struct iovec* iov, int iovcnt, off_t offset,
                     int flags) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    if (file->type == FILE_TYPE_RANDOM) {
        return (ssize_t)_file_readvRandomBytes(file, host, iov, iovcnt);
    }

    trace("File %i will preadv2 %d vector items from os-backed file %i at path "
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

    trace("File %i will write %zu bytes to os-backed file %i at path '%s'",
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

    trace("File %i will pwrite %zu bytes to os-backed file %i at path '%s'",
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

    trace("File %i will pwritev %d vector items from os-backed file %i at "
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

    trace("File %i will pwritev2 %d vector items from os-backed file %i at "
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

    trace("File %i fstat os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fstat(_file_getOSBackedFD(file), statbuf);
    return (result < 0) ? -errno : result;
}

int file_fstatfs(File* file, struct statfs* statbuf) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fstatfs os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fstatfs(_file_getOSBackedFD(file), statbuf);
    return (result < 0) ? -errno : result;
}

int file_fsync(File* file) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fsync os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fsync(_file_getOSBackedFD(file));
    return (result < 0) ? -errno : result;
}

int file_fchown(File* file, uid_t owner, gid_t group) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fchown os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fchown(_file_getOSBackedFD(file), owner, group);
    return (result < 0) ? -errno : result;
}

int file_fchmod(File* file, mode_t mode) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fchmod os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fchmod(_file_getOSBackedFD(file), mode);
    return (result < 0) ? -errno : result;
}

int file_ftruncate(File* file, off_t length) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i ftruncate os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = ftruncate(_file_getOSBackedFD(file), length);
    return (result < 0) ? -errno : result;
}

int file_fallocate(File* file, int mode, off_t offset, off_t length) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fallocate os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fallocate(_file_getOSBackedFD(file), mode, offset, length);
    return (result < 0) ? -errno : result;
}

int file_fadvise(File* file, off_t offset, off_t len, int advice) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fadvise os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = posix_fadvise(_file_getOSBackedFD(file), offset, len, advice);
    return (result < 0) ? -errno : result;
}

int file_flock(File* file, int operation) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i flock os-backed file %i", _file_getFD(file),
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

    trace("File %i fsetxattr os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fsetxattr(_file_getOSBackedFD(file), name, value, size, flags);
    return (result < 0) ? -errno : result;
}

ssize_t file_fgetxattr(File* file, const char* name, void* value, size_t size) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fgetxattr os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = fgetxattr(_file_getOSBackedFD(file), name, value, size);
    return (result < 0) ? -errno : result;
}

ssize_t file_flistxattr(File* file, char* list, size_t size) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i flistxattr os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = flistxattr(_file_getOSBackedFD(file), list, size);
    return (result < 0) ? -errno : result;
}

int file_fremovexattr(File* file, const char* name) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fremovexattr os-backed file %i", _file_getFD(file),
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

    trace("File %i sync_file_range os-backed file %i", _file_getFD(file),
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

    trace("File %i readahead os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = readahead(_file_getOSBackedFD(file), offset, count);
    return (result < 0) ? -errno : result;
}

off_t file_lseek(File* file, off_t offset, int whence) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i lseek os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    ssize_t result = lseek(_file_getOSBackedFD(file), offset, whence);
    return (result < 0) ? -errno : result;
}

int file_getdents(File* file, struct linux_dirent* dirp, unsigned int count) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i getdents os-backed file %i", _file_getFD(file),
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

    trace("File %i getdents64 os-backed file %i", _file_getFD(file),
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

    trace("File %i ioctl os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = ioctl(_file_getOSBackedFD(file), request, arg);
    return (result < 0) ? -errno : result;
}

int file_fcntl(File* file, unsigned long command, void* arg) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i fcntl os-backed file %i", _file_getFD(file),
          _file_getOSBackedFD(file));

    int result = fcntl(_file_getOSBackedFD(file), command, arg);
    return (result < 0) ? -errno : result;
}

int file_poll(File* file, struct pollfd* pfd) {
    MAGIC_ASSERT(file);

    if (!_file_getOSBackedFD(file)) {
        return -EBADF;
    }

    trace("File %i poll os-backed file %i", _file_getFD(file), _file_getOSBackedFD(file));

    // Don't let the OS block us
    int oldfd = pfd->fd;
    pfd->fd = _file_getOSBackedFD(file);
    int result = poll(pfd, (nfds_t)1, 0);
    pfd->fd = oldfd;
    return (result < 0) ? -errno : result;
}

///////////////////////////////////////////////
// *at functions (NULL directory file is valid)
///////////////////////////////////////////////

static inline int _file_getOSDirFD(File* dir) {
    if (dir) {
        MAGIC_ASSERT(dir);
        return dir->osfile.fd != OSFILE_INVALID ? dir->osfile.fd : AT_FDCWD;
    } else {
        return AT_FDCWD;
    }
}

int file_fstatat(File* dir, const char* pathname, struct stat* statbuf, int flags,
                 const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i fstatat os-backed file %i, flags %d", dir ? _file_getFD(dir) : -1, osFd, flags);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = fstatat(osFd, pathnameTmp, statbuf, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_fchownat(File* dir, const char* pathname, uid_t owner, gid_t group, int flags,
                  const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i fchownat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = fchownat(osFd, pathnameTmp, owner, group, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_fchmodat(File* dir, const char* pathname, mode_t mode, int flags, const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i fchmodat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = fchmodat(osFd, pathnameTmp, mode, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_futimesat(File* dir, const char* pathname, const struct timeval times[2],
                   const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i futimesat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = futimesat(osFd, pathnameTmp, times);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_utimensat(File* dir, const char* pathname, const struct timespec times[2], int flags,
                   const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i utimesat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = utimensat(osFd, pathnameTmp, times, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_faccessat(File* dir, const char* pathname, int mode, int flags, const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i faccessat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = faccessat(osFd, pathnameTmp, mode, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_mkdirat(File* dir, const char* pathname, mode_t mode, const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i mkdirat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = mkdirat(osFd, pathnameTmp, mode);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_mknodat(File* dir, const char* pathname, mode_t mode, dev_t dev, const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i mknodat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = mknodat(osFd, pathnameTmp, mode, dev);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_linkat(File* oldDir, const char* oldPath, File* newDir, const char* newPath, int flags,
                const char* workingDir) {
    int oldOsFd = _file_getOSDirFD(oldDir);
    int newOsFd = _file_getOSDirFD(newDir);
    const char* oldPathTmp = oldPath;
    const char* newPathTmp = newPath;

    trace("Files %i, %i linkat os-backed files %i, %i", oldDir ? _file_getFD(oldDir) : -1,
          newDir ? _file_getFD(newDir) : -1, oldOsFd, newOsFd);

    if (oldOsFd == AT_FDCWD) {
        oldOsFd = -1;
        oldPathTmp = _file_getAbsolutePath(NULL, oldPath, workingDir);
    }
    if (newOsFd == AT_FDCWD) {
        newOsFd = -1;
        newPathTmp = _file_getAbsolutePath(NULL, newPath, workingDir);
    }

    int result = linkat(oldOsFd, oldPathTmp, newOsFd, newPathTmp, flags);

    if (oldPathTmp != oldPath) {
        free((char*)oldPathTmp);
    }
    if (newPathTmp != newPath) {
        free((char*)newPathTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_unlinkat(File* dir, const char* pathname, int flags, const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i unlinkat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = unlinkat(osFd, pathnameTmp, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_symlinkat(File* dir, const char* linkpath, const char* target, const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* linkpathTmp = linkpath;

    trace("File %i symlinkat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        linkpathTmp = _file_getAbsolutePath(NULL, linkpath, workingDir);
    }

    int result = symlinkat(target, osFd, linkpathTmp);

    if (linkpathTmp != linkpath) {
        free((char*)linkpathTmp);
    }

    return (result < 0) ? -errno : result;
}

ssize_t file_readlinkat(File* dir, const char* pathname, char* buf, size_t bufsize,
                        const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i readlinkat os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    ssize_t result = readlinkat(osFd, pathnameTmp, buf, bufsize);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int file_renameat2(File* oldDir, const char* oldPath, File* newDir, const char* newPath,
                   unsigned int flags, const char* workingDir) {
    int oldOsFd = _file_getOSDirFD(oldDir);
    int newOsFd = _file_getOSDirFD(newDir);
    const char* oldPathTmp = oldPath;
    const char* newPathTmp = newPath;

    trace("Files %i, %i renameat2 os-backed files %i, %i", oldDir ? _file_getFD(oldDir) : -1,
          newDir ? _file_getFD(newDir) : -1, oldOsFd, newOsFd);

    if (oldOsFd == AT_FDCWD) {
        oldOsFd = -1;
        oldPathTmp = _file_getAbsolutePath(NULL, oldPath, workingDir);
    }

    if (newOsFd == AT_FDCWD) {
        newOsFd = -1;
        newPathTmp = _file_getAbsolutePath(NULL, newPath, workingDir);
    }

    int result = (int)syscall(SYS_renameat2, oldOsFd, oldPathTmp, newOsFd, newPathTmp, flags);

    if (oldPathTmp != oldPath) {
        free((char*)oldPathTmp);
    }
    if (newPathTmp != newPath) {
        free((char*)newPathTmp);
    }

    return (result < 0) ? -errno : result;
}

#ifdef SYS_statx
int file_statx(File* dir, const char* pathname, int flags, unsigned int mask,
               struct statx* statxbuf, const char* workingDir) {
    int osFd = _file_getOSDirFD(dir);
    const char* pathnameTmp = pathname;

    trace("File %i statx os-backed file %i", dir ? _file_getFD(dir) : -1, osFd);

    if (osFd == AT_FDCWD) {
        osFd = -1;
        pathnameTmp = _file_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = syscall(SYS_statx, osFd, pathnameTmp, flags, mask, statxbuf);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}
#endif
