/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/regular_file.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
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
#include "main/host/syscall/kernel_types.h"
#include "main/utility/utility.h"
#include "regular_file.h"

#define OSFILE_INVALID -1

const int SHADOW_FLAG_MASK = O_CLOEXEC;

/* Callback to (re)-generate contents of a `FILE_TYPE_IN_MEMORY` file.
 *
 * Should set `contents` to a malloc'd pointer containing the new contents, and
 * `contentsLen` to its length.
 *
 * Future uses might need more parameters or other flexibility, but let's not
 * add complexity we don't need yet.
 */
typedef void (*_generateInMemoryFileContentCbType)(char** contents, size_t* contentLen);

struct _RegularFile {
    /* File is a sub-type of a descriptor. */
    LegacyFile super;
    FileType type;
    /* O file flags that we don't pass to the native fd, but instead track within
     * Shadow and handle manually. A subset of SHADOW_FLAG_MASK. */
    int shadowFlags;
    /* Info related to our OS-backed file. */
    union {
        struct {
            int fd;
            /* The flags used when opening the file; Not the file's current flags. */
            int flagsAtOpen;
            /* The permission mode the file was opened with. */
            mode_t modeAtOpen;
            /* The path of the file when it was opened. */
            char* absPathAtOpen;
        } osfile;
        // For type=FILE_TYPE_IN_MEMORY
        struct {
            off_t cursor;
            ssize_t contentLen;
            char* content;
            /* The flags used when opening the file; Not the file's current flags. */
            int flagsAtOpen;
            /* The permission mode the file was opened with. */
            mode_t modeAtOpen;
            /* Callback to (re)-generate contents. */
            _generateInMemoryFileContentCbType generate_contents_cb;
            /* Whether contents should be re-generated on an lseek operation.
             * Typical for (some? all?) proc files
             *
             * TODO: Actually implement lseek for inMemoryFile and use this flag
             * to trigger regeneration via `generate_contents_cb`. lseek for
             * inMemoryFile currently just returns an error.
             */
            bool regen_after_lseek;
        } inMemoryFile;
    };
    MAGIC_DECLARE;
};

int regularfile_getFlagsAtOpen(RegularFile* file) {
    MAGIC_ASSERT(file);
    if (file->type != FILE_TYPE_IN_MEMORY) {
        return file->osfile.flagsAtOpen;
    } else {
        return file->inMemoryFile.flagsAtOpen;
    }
}

mode_t regularfile_getModeAtOpen(RegularFile* file) {
    MAGIC_ASSERT(file);
    if (file->type != FILE_TYPE_IN_MEMORY) {
        return file->osfile.modeAtOpen;
    } else {
        return file->inMemoryFile.modeAtOpen;
    }
}

int regularfile_getShadowFlags(RegularFile* file) {
    MAGIC_ASSERT(file);
    return file->shadowFlags;
}

FileType regularfile_getType(RegularFile* file) {
    MAGIC_ASSERT(file);
    return file->type;
}

static inline RegularFile* _regularfile_legacyFileToRegularFile(LegacyFile* desc) {
    utility_debugAssert(legacyfile_getType(desc) == DT_FILE);
    RegularFile* file = (RegularFile*)desc;
    MAGIC_ASSERT(file);
    return file;
}

static inline int _regularfile_getOSBackedFD(RegularFile* file) {
    MAGIC_ASSERT(file);
    if (file->type != FILE_TYPE_IN_MEMORY) {
        return file->osfile.fd;
    } else {
        // TODO verify calling site check for this value, some did check for 0
        // instead, which is somewhat valid
        // see https://github.com/shadow/shadow/issues/2604
        return OSFILE_INVALID;
    }
}

static inline bool _fd_isValid(int fd) { return fd >= 0; }

int regularfile_getOSBackedFD(RegularFile* file) { return _regularfile_getOSBackedFD(file); }

static void _regularfile_closeHelper(RegularFile* file) {
    if(file && file->type != FILE_TYPE_IN_MEMORY) {
        if (file && _fd_isValid(file->osfile.fd)) {
            trace("On file %p, closing os-backed file %i", file, _regularfile_getOSBackedFD(file));

            close(file->osfile.fd);
            file->osfile.fd = OSFILE_INVALID;

            /* The os-backed file is no longer ready. */
            legacyfile_adjustStatus(&file->super, FileState_ACTIVE, FALSE, 0);
        }
    }
}

static void _regularfile_close(LegacyFile* desc, const Host* host) {
    RegularFile* file = _regularfile_legacyFileToRegularFile(desc);

    trace("Closing file %p with os-backed file %i", file, _regularfile_getOSBackedFD(file));

    /* Make sure we mimic the close on the OS-backed file now. */
    _regularfile_closeHelper(file);
}

static void _regularfile_free(LegacyFile* desc) {
    RegularFile* file = _regularfile_legacyFileToRegularFile(desc);

    trace("Freeing file %p with os-backed file %i", file, _regularfile_getOSBackedFD(file));

    _regularfile_closeHelper(file);

    if (file->type != FILE_TYPE_IN_MEMORY && file->osfile.absPathAtOpen) {
        free(file->osfile.absPathAtOpen);
    }

    if (file->type == FILE_TYPE_IN_MEMORY && file->inMemoryFile.content != NULL) {
        free(file->inMemoryFile.content);
    }

    legacyfile_clear((LegacyFile*)file);
    MAGIC_CLEAR(file);
    free(file);

    worker_count_deallocation(RegularFile);
}

static LegacyFileFunctionTable _fileFunctions = (LegacyFileFunctionTable){
    .close = _regularfile_close,
    .cleanup = NULL,
    .free = _regularfile_free,
};

RegularFile* regularfile_new() {
    RegularFile* file = malloc(sizeof(RegularFile));

    *file = (RegularFile){0};

    legacyfile_init(&(file->super), DT_FILE, &_fileFunctions);
    MAGIC_INIT(file);
    file->osfile.fd = OSFILE_INVALID; // negative means uninitialized (0 is a valid fd)

    worker_count_allocation(RegularFile);
    return file;
}

static char* _regularfile_getConcatStr(const char* prefix, const char sep, const char* suffix) {
    char* path = NULL;
    if (asprintf(&path, "%s%c%s", prefix, sep, suffix) < 0) {
        utility_panic("asprintf could not allocate a buffer, error %i: %s", errno, strerror(errno));
        abort();
    }
    return path;
}

static char* _regularfile_getAbsolutePath(RegularFile* dir, const char* pathname,
                                          const char* workingDir) {
    utility_debugAssert(pathname);
    utility_debugAssert(workingDir);
    utility_debugAssert(workingDir[0] == '/');

    /* Compute the absolute path, which will allow us to reopen later. */
    if (pathname[0] == '/') {
        /* The path is already absolute. Just copy it. */
        return strdup(pathname);
    }

    /* The path is relative, try dir prefix first. */
    if (dir && dir->type != FILE_TYPE_IN_MEMORY && dir->osfile.absPathAtOpen) {
        return _regularfile_getConcatStr(dir->osfile.absPathAtOpen, '/', pathname);
    }

    /* Use current working directory as prefix. */
    char* abspath = _regularfile_getConcatStr(workingDir, '/', pathname);
    return abspath;
}

#ifdef DEBUG
#define CHECK_FLAG(flag)                                                                           \
    if (flags & flag) {                                                                            \
        if (!flag_str) {                                                                           \
            int rv = asprintf(&flag_str, #flag);                                                   \
            utility_alwaysAssert(rv >= 0);                                                         \
        } else {                                                                                   \
            char* str = _regularfile_getConcatStr(flag_str, '|', #flag);                           \
            free(flag_str);                                                                        \
            flag_str = str;                                                                        \
        }                                                                                          \
    }
static void _regularfile_print_flags(int flags) {
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
        int rv = asprintf(&flag_str, "0");
        utility_alwaysAssert(rv >= 0);
    }
    trace("Found flags: %s", flag_str);
    if (flag_str) {
        free(flag_str);
    }
}
#undef CHECK_FLAG
#endif

int _regularfile_initRoInMemoryFile(RegularFile* file, int flags, mode_t mode,
                                    _generateInMemoryFileContentCbType generate_contents_cb,
                                    bool regen_after_lseek) {
    if (flags & O_DIRECTORY) {
        return -ENOTDIR;
    }

    if (flags & O_WRONLY || flags & O_RDWR) {
        return -EPERM;
    }

    size_t contentLen = 0;
    char* content = NULL;
    generate_contents_cb(&content, &contentLen);
    utility_alwaysAssert(content != NULL);

    file->type = FILE_TYPE_IN_MEMORY;
    file->inMemoryFile.cursor = 0;
    file->inMemoryFile.cursor = 0;
    file->inMemoryFile.contentLen = contentLen;
    file->inMemoryFile.content = content;
    file->inMemoryFile.flagsAtOpen = flags;
    file->inMemoryFile.modeAtOpen = mode;
    file->inMemoryFile.generate_contents_cb = generate_contents_cb;
    file->inMemoryFile.regen_after_lseek = regen_after_lseek;

    return 0;
}

// For populating "/sys/devices/system/cpu/possible" and "/sys/devices/system/cpu/online".
void _generate_cpu_possible_or_online(char** contents, size_t* contents_len) {
    *contents = malloc(2);
    (*contents)[0] = '0';
    (*contents)[1] = '\n';
    *contents_len = 2;
}

// For populating "/proc/sys/kernel/random/uuid"
void _generate_random_uuid(char** contents, size_t* contents_len) {
    unsigned char bytes[16] = {0};
    host_rngNextNBytes(worker_getCurrentHost(), bytes, sizeof(bytes));

    *contents_len = 16 /*bytes*/ * 2 /*chars-per-byte*/ + 4 /*dashes*/ + 1 /*newline*/;
    *contents = malloc(*contents_len + 1); /* allocate enough room for the NUL byte, too */
    int n = snprintf(*contents, *contents_len + 1,
                     "%02hhx%02hhx%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx-%02hhx%02hhx"
                     "%02hhx%02hhx%02hhx%02hhx\n",
                     bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
                     bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14],
                     bytes[15]);
    utility_alwaysAssert(n == *contents_len);
}

int regularfile_openat(RegularFile* file, RegularFile* dir, const char* pathname, int flags,
                       mode_t mode, const char* workingDir) {
    MAGIC_ASSERT(file);
    utility_debugAssert(file->type == FILE_TYPE_NOTSET && file->osfile.fd == OSFILE_INVALID);

    trace("Attempting to open file with pathname=%s flags=%i mode=%i workingdir=%s", pathname,
          flags, (int)mode, workingDir);
#ifdef DEBUG
    if (flags) {
        _regularfile_print_flags(flags);
    }
#endif

    /* The default case is a regular file. We do this first so that we have
     * an absolute path to compare for special files. */
    char* abspath = _regularfile_getAbsolutePath(dir, pathname, workingDir);

    const char* proc_prefix = "/proc/";

    /* Handle special files. */
    if (utility_isRandomPath(abspath)) {
        file->type = FILE_TYPE_RANDOM;
    } else if (!strcmp("/etc/hosts", abspath)) {
        file->type = FILE_TYPE_HOSTS;
        const char* hostspath = worker_getHostsFilePath();
        if (hostspath && abspath) {
            free(abspath);
            abspath = strdup(hostspath);
        }
        worker_freeHostsFilePath(hostspath);
    } else if (!strcmp("/etc/localtime", abspath)) {
        file->type = FILE_TYPE_LOCALTIME;
        if (abspath) {
            free(abspath);
        }
        // Shadow time is in UTC.
        abspath = strdup("/usr/share/zoneinfo/Etc/UTC");
    } else if (!strcmp("/sys/devices/system/cpu/possible", abspath) ||
               !strcmp("/sys/devices/system/cpu/online", abspath)) {
        if (abspath) {
            free(abspath);
        }
        return _regularfile_initRoInMemoryFile(
            file, flags, mode, _generate_cpu_possible_or_online, false);
    } else if (!strcmp("/proc/sys/kernel/random/uuid", abspath)) {
        if (abspath) {
            free(abspath);
        }
        return _regularfile_initRoInMemoryFile(file, flags, mode, _generate_random_uuid, true);
    } else if (!strncmp(proc_prefix, abspath, strlen(proc_prefix))) {
        file->type = FILE_TYPE_REGULAR;
        if (!strcmp(abspath, "/proc/self/maps")) {
            // Should work as intended, with the /proc/self remapping below.
            // The contents aren't *quite* 100% deterministic, because we'll
            // have mapped in different `/dev/shm/shadow_shmemfile_*` files on
            // each run.  These differences are unlikely to cascade into further
            // non-determinism, though.
        } else if (!strcmp(abspath, "/proc/self/exe")) {
            // Should work as intended, with the /proc/self remapping below.
        } else {
            // Might work out ok, but we haven't specifically vetted.
            warning("Opening unsupported proc file. Contents may incorrectly refer to native "
                    "process instead of emulated, and/or have nondeterministic contents: %s",
                    abspath);
        }
        // Remap `/proc/self/` prefixes.
        const char* proc_self_prefix = "/proc/self/";
        if (!strncmp(proc_self_prefix, abspath, strlen(proc_self_prefix))) {
            const Process* proc = worker_getCurrentProcess();
            pid_t pid = process_getNativePid(proc);
            char* new_path = malloc(PATH_MAX);
            int rv = snprintf(
                new_path, PATH_MAX, "/proc/%d/%s", pid, &abspath[strlen(proc_self_prefix)]);
            if (rv >= PATH_MAX) {
                warning("Couldn't replace `self` with pid; result was too long: %s", abspath);
                file->type = FILE_TYPE_NOTSET;
                return -ENAMETOOLONG;
            }
            debug("Rewriting `openat` path '%s' to '%s'", abspath, new_path);
            free(abspath);
            abspath = new_path;
        }
        // TODO:
        // * Remap /proc/thread-self/*
        // * Remap /proc/[tid]/*
        // * Remap /proc/[tid]/task/[tid]/*
        // * Handle a lot of these as special files or directories instead of
        //   allowing direct access. Notably including:
        //   * /proc/[tid]/task/ Needs to list virtual child [tid]s
        //   * /proc/[tid]/fd/ Needs to list virtual file descriptors
        // * Probably much more ...
    } else {
        file->type = FILE_TYPE_REGULAR;
    }

    int originalFlags = flags;

    // move any flags that shadow handles from 'flags' to 'shadowFlags'
    file->shadowFlags = flags & SHADOW_FLAG_MASK;
    flags &= ~SHADOW_FLAG_MASK;

    // we should always use O_CLOEXEC for files opened in shadow
    flags |= O_CLOEXEC;

    // TODO: we should open the os-backed file in non-blocking mode even if a
    // non-block is not requested, and then properly handle the io by, e.g.,
    // epolling on all such files with a shadow support thread.
    int osfd = open(abspath, flags, mode);
    int errcode = errno;

    if (osfd < 0) {
        trace("RegularFile %p opening path '%s' returned %i: %s", file, abspath, osfd,
              strerror(errcode));
        if (abspath) {
            free(abspath);
        }
        file->type = FILE_TYPE_NOTSET;
        return -errcode;
    }

    /* Store the create information, which is used if we mmap the file later. */
    file->osfile.fd = osfd;
    file->osfile.absPathAtOpen = abspath;
    file->osfile.flagsAtOpen = flags;
    file->osfile.modeAtOpen = mode;

    trace("RegularFile %p opened os-backed file %i at absolute path %s", file,
          _regularfile_getOSBackedFD(file), file->osfile.absPathAtOpen);

    /* The os-backed file is now ready. */
    legacyfile_adjustStatus(&file->super, FileState_ACTIVE, TRUE, 0);

    return 0;
}

int regularfile_open(RegularFile* file, const char* pathname, int flags, mode_t mode,
                     const char* workingDir) {
    return regularfile_openat(file, NULL, pathname, flags, mode, workingDir);
}

static void _regularfile_readRandomBytes(RegularFile* file, const Host* host, void* buf,
                                         size_t numBytes) {
    utility_debugAssert(file->type == FILE_TYPE_RANDOM);

    utility_debugAssert(host != NULL);

    trace("RegularFile %p will read %zu bytes from random source for host %s", file, numBytes,
          host_getName(host));

    host_rngNextNBytes(host, buf, numBytes);
}

static size_t _regularfile_readvRandomBytes(RegularFile* file, const Host* host,
                                            const struct iovec* iov, int iovcnt) {
    size_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        _regularfile_readRandomBytes(file, host, iov[i].iov_base, iov[i].iov_len);
        total += iov[i].iov_len;
    }
    return total;
}

ssize_t regularfile_read(RegularFile* file, const Host* host, void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_RANDOM) {
        _regularfile_readRandomBytes(file, host, buf, bufSize);
        return (ssize_t)bufSize;
    }

    if (file->type == FILE_TYPE_IN_MEMORY) {
        ssize_t read = regularfile_pread(file, host, buf, bufSize, file->inMemoryFile.cursor);
        file->inMemoryFile.cursor += read;
        return read;
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will read %zu bytes from os-backed file %i at path '%s'", file, bufSize,
          _regularfile_getOSBackedFD(file), file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = read(_regularfile_getOSBackedFD(file), buf, bufSize);
    return (result < 0) ? -errno : result;
}

ssize_t regularfile_pread(RegularFile* file, const Host* host, void* buf, size_t bufSize,
                          off_t offset) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_RANDOM) {
        _regularfile_readRandomBytes(file, host, buf, bufSize);
        return (ssize_t)bufSize;
    }

    if (file->type == FILE_TYPE_IN_MEMORY) {
        struct iovec iov[1];
        ssize_t nwritten;
        iov[0].iov_base = buf;
        iov[0].iov_len = bufSize;
        return regularfile_preadv(file, host, iov, 1, offset);
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will pread %zu bytes from os-backed file %i offset %ld at path '%s'",
          file, bufSize, _regularfile_getOSBackedFD(file), offset, file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pread(_regularfile_getOSBackedFD(file), buf, bufSize, offset);
    return (result < 0) ? -errno : result;
}

ssize_t regularfile_preadv(RegularFile* file, const Host* host, const struct iovec* iov, int iovcnt,
                           off_t offset) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_RANDOM) {
        return (ssize_t)_regularfile_readvRandomBytes(file, host, iov, iovcnt);
    }

    if (file->type == FILE_TYPE_IN_MEMORY) {
        if (file->inMemoryFile.content == NULL) {
            return -EBADF;
        }
        if (iovcnt == 0) {
            return 0;
        }
        if (iov == NULL) {
            return -EINVAL;
        }
        ssize_t read = 0;
        for (int i = 0; i < iovcnt; i++) {
            if (iov[i].iov_len == 0) {
                continue;
            }
            if (iov[i].iov_base == NULL) {
                return -EINVAL;
            }
            ssize_t left = file->inMemoryFile.contentLen - offset;
            if (!left) {
                break;
            }
            ssize_t to_read = MIN(left, iov[i].iov_len);
            memcpy(iov[i].iov_base, file->inMemoryFile.content + offset, to_read);
            offset += to_read;
            read += to_read;
        }

        return read;
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will preadv %d vector items from os-backed file %i at path '%s'", file,
          iovcnt, _regularfile_getOSBackedFD(file), file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = preadv(_regularfile_getOSBackedFD(file), iov, iovcnt, offset);
    return (result < 0) ? -errno : result;
}

#ifdef SYS_preadv2
ssize_t regularfile_preadv2(RegularFile* file, const Host* host, const struct iovec* iov,
                            int iovcnt, off_t offset, int flags) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_RANDOM) {
        return (ssize_t)_regularfile_readvRandomBytes(file, host, iov, iovcnt);
    }

    if (file->type == FILE_TYPE_IN_MEMORY) {
        // flags can be ignored: none really impart in memory files
        return regularfile_preadv(file, host, iov, iovcnt, offset);
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will preadv2 %d vector items from os-backed file %i at path '%s'", file,
          iovcnt, _regularfile_getOSBackedFD(file), file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result =
        preadv2(_regularfile_getOSBackedFD(file), iov, iovcnt, offset, flags);
    return (result < 0) ? -errno : result;
}
#endif

ssize_t regularfile_write(RegularFile* file, const void* buf, size_t bufSize) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_IN_MEMORY) {
        return -EBADF;
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will write %zu bytes to os-backed file %i at path '%s'", file, bufSize,
          _regularfile_getOSBackedFD(file), file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = write(_regularfile_getOSBackedFD(file), buf, bufSize);
    return (result < 0) ? -errno : result;
}

ssize_t regularfile_pwrite(RegularFile* file, const void* buf, size_t bufSize, off_t offset) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_IN_MEMORY) {
        return -EBADF;
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will pwrite %zu bytes to os-backed file %i offset %ld at path '%s'", file,
          bufSize, _regularfile_getOSBackedFD(file), offset, file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pwrite(_regularfile_getOSBackedFD(file), buf, bufSize, offset);
    return (result < 0) ? -errno : result;
}

ssize_t regularfile_pwritev(RegularFile* file, const struct iovec* iov, int iovcnt, off_t offset) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_IN_MEMORY) {
        return -EBADF;
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will pwritev %d vector items from os-backed file %i at path '%s'", file,
          iovcnt, _regularfile_getOSBackedFD(file), file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result = pwritev(_regularfile_getOSBackedFD(file), iov, iovcnt, offset);
    return (result < 0) ? -errno : result;
}

#ifdef SYS_pwritev2
ssize_t regularfile_pwritev2(RegularFile* file, const struct iovec* iov, int iovcnt, off_t offset,
                             int flags) {
    MAGIC_ASSERT(file);

    if (file->type == FILE_TYPE_IN_MEMORY) {
        return -EBADF;
    }

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p will pwritev2 %d vector items from os-backed file %i at path '%s'", file,
          iovcnt, _regularfile_getOSBackedFD(file), file->osfile.absPathAtOpen);

    /* TODO: this may block the shadow thread until we properly handle
     * os-backed files in non-blocking mode. */
    ssize_t result =
        pwritev2(_regularfile_getOSBackedFD(file), iov, iovcnt, offset, flags);
    return (result < 0) ? -errno : result;
}
#endif

int regularfile_fstat(RegularFile* file, struct stat* statbuf) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fstat os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fstat(_regularfile_getOSBackedFD(file), statbuf);
    return (result < 0) ? -errno : result;
}

int regularfile_fstatfs(RegularFile* file, struct statfs* statbuf) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fstatfs os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fstatfs(_regularfile_getOSBackedFD(file), statbuf);
    return (result < 0) ? -errno : result;
}

int regularfile_fsync(RegularFile* file) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fsync os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fsync(_regularfile_getOSBackedFD(file));
    return (result < 0) ? -errno : result;
}

int regularfile_fchown(RegularFile* file, uid_t owner, gid_t group) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fchown os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fchown(_regularfile_getOSBackedFD(file), owner, group);
    return (result < 0) ? -errno : result;
}

int regularfile_fchmod(RegularFile* file, mode_t mode) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fchmod os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fchmod(_regularfile_getOSBackedFD(file), mode);
    return (result < 0) ? -errno : result;
}

int regularfile_ftruncate(RegularFile* file, off_t length) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p ftruncate os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = ftruncate(_regularfile_getOSBackedFD(file), length);
    return (result < 0) ? -errno : result;
}

int regularfile_fallocate(RegularFile* file, int mode, off_t offset, off_t length) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fallocate os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fallocate(_regularfile_getOSBackedFD(file), mode, offset, length);
    return (result < 0) ? -errno : result;
}

int regularfile_fadvise(RegularFile* file, off_t offset, off_t len, int advice) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fadvise os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = posix_fadvise(_regularfile_getOSBackedFD(file), offset, len, advice);
    return (result < 0) ? -errno : result;
}

int regularfile_flock(RegularFile* file, int operation) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p flock os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = flock(_regularfile_getOSBackedFD(file), operation);
    return (result < 0) ? -errno : result;
}

int regularfile_fsetxattr(RegularFile* file, const char* name, const void* value, size_t size,
                          int flags) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fsetxattr os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fsetxattr(_regularfile_getOSBackedFD(file), name, value, size, flags);
    return (result < 0) ? -errno : result;
}

ssize_t regularfile_fgetxattr(RegularFile* file, const char* name, void* value, size_t size) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fgetxattr os-backed file %i", file, _regularfile_getOSBackedFD(file));

    ssize_t result = fgetxattr(_regularfile_getOSBackedFD(file), name, value, size);
    return (result < 0) ? -errno : result;
}

ssize_t regularfile_flistxattr(RegularFile* file, char* list, size_t size) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p flistxattr os-backed file %i", file, _regularfile_getOSBackedFD(file));

    ssize_t result = flistxattr(_regularfile_getOSBackedFD(file), list, size);
    return (result < 0) ? -errno : result;
}

int regularfile_fremovexattr(RegularFile* file, const char* name) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p fremovexattr os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = fremovexattr(_regularfile_getOSBackedFD(file), name);
    return (result < 0) ? -errno : result;
}

int regularfile_sync_range(RegularFile* file, off64_t offset, off64_t nbytes, unsigned int flags) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace(
        "RegularFile %p sync_file_range os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result =
        sync_file_range(_regularfile_getOSBackedFD(file), offset, nbytes, flags);
    return (result < 0) ? -errno : result;
}

ssize_t regularfile_readahead(RegularFile* file, off64_t offset, size_t count) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p readahead os-backed file %i", file, _regularfile_getOSBackedFD(file));

    ssize_t result = readahead(_regularfile_getOSBackedFD(file), offset, count);
    return (result < 0) ? -errno : result;
}

off_t regularfile_lseek(RegularFile* file, off_t offset, int whence) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p lseek os-backed file %i", file, _regularfile_getOSBackedFD(file));

    ssize_t result = lseek(_regularfile_getOSBackedFD(file), offset, whence);
    return (result < 0) ? -errno : result;
}

int regularfile_getdents(RegularFile* file, struct linux_dirent* dirp, unsigned int count) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p getdents os-backed file %i", file, _regularfile_getOSBackedFD(file));

    // getdents is not available for a direct call
    int result =
        (int)syscall(SYS_getdents, _regularfile_getOSBackedFD(file), dirp, count);
    return (result < 0) ? -errno : result;
}

int regularfile_getdents64(RegularFile* file, struct linux_dirent64* dirp,
                    unsigned int count) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p getdents64 os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result =
        (int)syscall(SYS_getdents64, _regularfile_getOSBackedFD(file), dirp, count);
    return (result < 0) ? -errno : result;
}

int regularfile_ioctl(RegularFile* file, unsigned long request, void* arg) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p ioctl os-backed file %i", file, _regularfile_getOSBackedFD(file));

    int result = ioctl(_regularfile_getOSBackedFD(file), request, arg);
    return (result < 0) ? -errno : result;
}

int regularfile_fcntl(RegularFile* file, unsigned long command, void* arg) {
    MAGIC_ASSERT(file);

    trace("RegularFile %p fcntl os-backed file %i", file, _regularfile_getOSBackedFD(file));

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    if (command == F_SETFD) {
        intptr_t arg_int = (intptr_t)arg;
        // if the arg contains FD_CLOEXEC
        if (arg_int & FD_CLOEXEC) {
            file->shadowFlags |= O_CLOEXEC;
        } else {
            file->shadowFlags &= ~O_CLOEXEC;
        }
        // shadow always sets FD_CLOEXEC on the os-backed fd
        arg_int |= FD_CLOEXEC;
        arg = (void*)arg_int;
    }

    int result = fcntl(_regularfile_getOSBackedFD(file), command, arg);

    if (result >= 0 && command == F_GETFD) {
        // if the file should have FD_CLOEXEC
        if (file->shadowFlags & O_CLOEXEC) {
            result |= FD_CLOEXEC;
        } else {
            result &= ~FD_CLOEXEC;
        }
    }

    return (result < 0) ? -errno : result;
}

int regularfile_poll(RegularFile* file, struct pollfd* pfd) {
    MAGIC_ASSERT(file);

    if (!_fd_isValid(_regularfile_getOSBackedFD(file))) {
        return -EBADF;
    }

    trace("RegularFile %p poll os-backed file %i", file, _regularfile_getOSBackedFD(file));

    // Don't let the OS block us
    int oldfd = pfd->fd;
    pfd->fd = _regularfile_getOSBackedFD(file);
    int result = poll(pfd, (nfds_t)1, 0);
    pfd->fd = oldfd;
    return (result < 0) ? -errno : result;
}

///////////////////////////////////////////////
// *at functions (NULL directory file is valid)
///////////////////////////////////////////////

// May return an error (negative value), as not all `RegularFile`s in shadow have an OS fd.
static int _regularfile_getOSDirFD(RegularFile* dir, int *outFd) {
    if (dir) {
        MAGIC_ASSERT(dir);
        switch (dir->type) {
            case FILE_TYPE_IN_MEMORY:
                // No OS file, so nothing we can do here.
                return -1;
            case FILE_TYPE_NOTSET:
            case FILE_TYPE_RANDOM:
            case FILE_TYPE_HOSTS:
            case FILE_TYPE_LOCALTIME:
            case FILE_TYPE_REGULAR:
                if (dir->osfile.fd == OSFILE_INVALID) {
                    // No OS file, so nothing we can do here.
                    return -1;
                }
                *outFd = dir->osfile.fd;
                return 0;
        }
        panic("Didn't check all file types (and compiler should have warned about this)");
    } else {
        // No directory file provided, so use the cwd.
        *outFd = AT_FDCWD;
        return 0;
    }
}

int regularfile_fstatat(RegularFile* dir, const char* pathname, struct stat* statbuf, int flags,
                        const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_fstatat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p fstatat os-backed file %i, flags %d", dir, osFd, flags);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0 && !(flags & AT_EMPTY_PATH)) {
            // stat(2):
            // > ENOENT - path is an empty string and AT_EMPTY_PATH was not
            // > specified in flags.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = fstatat(osFd, pathnameTmp, statbuf, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_fchownat(RegularFile* dir, const char* pathname, uid_t owner, gid_t group,
                         int flags, const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_fchownat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p fchownat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0 && !(flags & AT_EMPTY_PATH)) {
            // Unlike fstatat, the man page for fchownat does not appear to
            // specify what happens when the path name is empty. But fchownat
            // does have an `AT_EMPTY_PATH` flag and experimentally seems to
            // behave similarly to fstatat.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = fchownat(osFd, pathnameTmp, owner, group, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_fchmodat(RegularFile* dir, const char* pathname, mode_t mode, int flags,
                         const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_fchmodat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p fchmodat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0) {
            // The man page does not appear to specify what happens when the
            // path name is empty. But it experimentally seems to return an
            // error.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = fchmodat(osFd, pathnameTmp, mode, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_futimesat(RegularFile* dir, const char* pathname, const struct timeval times[2],
                          const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_futimesat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p futimesat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0) {
            // The man page does not appear to specify what happens when the
            // path name is empty. But it experimentally seems to return an
            // error.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = futimesat(osFd, pathnameTmp, times);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_utimensat(RegularFile* dir, const char* pathname, const struct timespec times[2],
                          int flags, const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_utimensat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p utimesat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0 && !(flags & AT_EMPTY_PATH)) {
            // utimensat(2):
            // > ENOENT - (utimensat()) A component of pathname does not refer to
            // > an existing directory or file, or pathname is an empty string
            //
            // Presumably it does want to allow an empty path if
            // `AT_EMPTY_PATH` is set.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = utimensat(osFd, pathnameTmp, times, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_faccessat(RegularFile* dir, const char* pathname, int mode, int flags,
                          const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_faccessat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p faccessat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0 && !(flags & AT_EMPTY_PATH)) {
            // Unlike fstatat, the man page does not appear to specify what
            // happens when the path name is empty. But it does have an
            // `AT_EMPTY_PATH` flag and experimentally seems to behave
            // similarly to fstatat.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = faccessat(osFd, pathnameTmp, mode, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_mkdirat(RegularFile* dir, const char* pathname, mode_t mode,
                        const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_mkdirat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p mkdirat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0) {
            // The man page does not appear to specify what happens when the path
            // name is empty. But it experimentally seems to return ENOENT.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = mkdirat(osFd, pathnameTmp, mode);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_mknodat(RegularFile* dir, const char* pathname, mode_t mode, dev_t dev,
                        const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_mknodat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p mknodat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0) {
            // The man page does not appear to specify what happens when the path
            // name is empty. But it experimentally seems to return ENOENT.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = mknodat(osFd, pathnameTmp, mode, dev);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_linkat(RegularFile* oldDir, const char* oldPath, RegularFile* newDir,
                       const char* newPath, int flags, const char* workingDir) {
    int oldOsFd = -1;
    int newOsFd = -1;
    if (_regularfile_getOSDirFD(oldDir, &oldOsFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_linkat'");
        return -EINVAL;
    }
    if (_regularfile_getOSDirFD(newDir, &newOsFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_linkat'");
        return -EINVAL;
    }

    const char* oldPathTmp = oldPath;
    const char* newPathTmp = newPath;

    trace("RegularFiles %p, %p linkat os-backed files %i, %i", oldDir, newDir, oldOsFd, newOsFd);

    // TODO: properly handle an empty path

    if (oldOsFd == AT_FDCWD) {
        oldOsFd = -1;
        oldPathTmp = _regularfile_getAbsolutePath(NULL, oldPath, workingDir);
    }
    if (newOsFd == AT_FDCWD) {
        newOsFd = -1;
        newPathTmp = _regularfile_getAbsolutePath(NULL, newPath, workingDir);
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

int regularfile_unlinkat(RegularFile* dir, const char* pathname, int flags,
                         const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_unlinkat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p unlinkat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0) {
            // unlinkat(2):
            // > ENOENT - A component in pathname does not exist or is a dangling
            // > symbolic link, or pathname is empty.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = unlinkat(osFd, pathnameTmp, flags);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_symlinkat(RegularFile* dir, const char* linkpath, const char* target,
                          const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_symlinkat'");
        return -EINVAL;
    }

    const char* linkpathTmp = linkpath;

    trace("RegularFile %p symlinkat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(linkpathTmp) == 0) {
            // symlinkat(2):
            // > ENOENT - A directory component in linkpath does not exist or is a
            // > dangling symbolic link, or target or linkpath is an empty string.
            return -ENOENT;
        }

        osFd = -1;
        linkpathTmp = _regularfile_getAbsolutePath(NULL, linkpath, workingDir);
    }

    int result = symlinkat(target, osFd, linkpathTmp);

    if (linkpathTmp != linkpath) {
        free((char*)linkpathTmp);
    }

    return (result < 0) ? -errno : result;
}

ssize_t regularfile_readlinkat(RegularFile* dir, const char* pathname, char* buf, size_t bufsize,
                               const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_readlinkat'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p readlinkat os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0) {
            // readlinkat(2):
            // > Since Linux 2.6.39, pathname can be an empty string, in which
            // > case the call operates on the symbolic link referred to by
            // > dirfd (which should have been obtained using open(2) with the
            // > O_PATH and O_NOFOLLOW flags).
            //
            // If both AT_FDCWD and "" were specified, the call operates on the
            // current working directory, which shouldn't be a symlink. It
            // experimentally seems to return ENOENT instead of EINVAL.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    ssize_t result = readlinkat(osFd, pathnameTmp, buf, bufsize);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}

int regularfile_renameat2(RegularFile* oldDir, const char* oldPath, RegularFile* newDir,
                          const char* newPath, unsigned int flags, const char* workingDir) {
    int oldOsFd = -1;
    int newOsFd = -1;
    if (_regularfile_getOSDirFD(oldDir, &oldOsFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_renameat2'");
        return -EINVAL;
    }
    if (_regularfile_getOSDirFD(newDir, &newOsFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_renameat2'");
        return -EINVAL;
    }

    const char* oldPathTmp = oldPath;
    const char* newPathTmp = newPath;

    trace("RegularFiles %p, %p renameat2 os-backed files %i, %i", oldDir, newDir, oldOsFd, newOsFd);

    // TODO: properly handle an empty path

    if (oldOsFd == AT_FDCWD) {
        oldOsFd = -1;
        oldPathTmp = _regularfile_getAbsolutePath(NULL, oldPath, workingDir);
    }

    if (newOsFd == AT_FDCWD) {
        newOsFd = -1;
        newPathTmp = _regularfile_getAbsolutePath(NULL, newPath, workingDir);
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
int regularfile_statx(RegularFile* dir, const char* pathname, int flags, unsigned int mask,
                      struct statx* statxbuf, const char* workingDir) {
    int osFd = -1;
    if (_regularfile_getOSDirFD(dir, &osFd) < 0) {
        // this would probably be a 'warn-once-then-debug' in rust
        debug("Failed to get OS fd for 'RegularFile' in 'regularfile_statx'");
        return -EINVAL;
    }

    const char* pathnameTmp = pathname;

    trace("RegularFile %p statx os-backed file %i", dir, osFd);

    if (osFd == AT_FDCWD) {
        if (strlen(pathnameTmp) == 0 && !(flags & AT_EMPTY_PATH)) {
            // stat(2):
            // > ENOENT - A component of pathname does not exist, or pathname
            // > is an empty string and AT_EMPTY_PATH was not specified in
            // > flags.
            return -ENOENT;
        }

        osFd = -1;
        pathnameTmp = _regularfile_getAbsolutePath(NULL, pathname, workingDir);
    }

    int result = syscall(SYS_statx, osFd, pathnameTmp, flags, mask, statxbuf);

    if (pathnameTmp != pathname) {
        free((char*)pathnameTmp);
    }

    return (result < 0) ? -errno : result;
}
#endif
