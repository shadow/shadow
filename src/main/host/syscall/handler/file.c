/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/handler/file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall/protected.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateFileHelper(SyscallHandler* sys, int filefd,
                                              RegularFile** file_desc_out) {
    /* Check that fd is within bounds. */
    if (filefd < 0) {
        debug("descriptor %i out of bounds", filefd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    LegacyFile* desc = thread_getRegisteredLegacyFile(rustsyscallhandler_getThread(sys), filefd);
    if (desc && file_desc_out) {
        *file_desc_out = (RegularFile*)desc;
    }

    int errcode = _syscallhandler_validateLegacyFile(desc, DT_FILE);
    if (errcode) {
        debug("descriptor %i is invalid", filefd);
        return errcode;
    }

    /* Now we know we have a valid file. */
    return 0;
}

static SyscallReturn _syscallhandler_openHelper(SyscallHandler* sys, UntypedForeignPtr pathnamePtr,
                                                int flags, mode_t mode) {
    trace("Trying to open file with path name at plugin addr %p",
          (void*)pathnamePtr.val);

    /* Get the path string from the plugin. */
    char pathname[PATH_MAX];
    int errcode = process_readString(
        rustsyscallhandler_getProcess(sys), pathname, pathnamePtr, sizeof(pathname));
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Create and open the file. */
    RegularFile* filed = regularfile_new();
    errcode = regularfile_open(filed, pathname, flags & ~O_CLOEXEC, mode,
                               process_getWorkingDir(rustsyscallhandler_getProcess(sys)));

    if (errcode < 0) {
        /* This will unref/free the RegularFile. */
        legacyfile_close((LegacyFile*)filed, rustsyscallhandler_getHost(sys));
        legacyfile_unref(filed);
        return syscallreturn_makeDoneErrno(-errcode);
    }

    utility_debugAssert(errcode == 0);
    Descriptor* desc = descriptor_fromLegacyFile((LegacyFile*)filed, flags & O_CLOEXEC);
    int handle = thread_registerDescriptor(rustsyscallhandler_getThread(sys), desc);
    return syscallreturn_makeDoneI64(handle);
}

static SyscallReturn _syscallhandler_fsyncHelper(SyscallHandler* sys, int fd) {
    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_fsync(file_desc));
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_creat(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_openHelper(sys, args->args[0].as_ptr,
                                      O_CREAT | O_WRONLY | O_TRUNC,
                                      args->args[1].as_u64);
}

SyscallReturn syscallhandler_open(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_openHelper(
        sys, args->args[0].as_ptr, args->args[1].as_i64, args->args[2].as_u64);
}

SyscallReturn syscallhandler_fstat(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr bufPtr = args->args[1].as_ptr; // struct stat*

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get some memory in which to return the result. */
    struct stat buf = {0};

    int res = regularfile_fstat(file_desc, &buf);
    if (res < 0) {
        return syscallreturn_makeDoneErrno(-res);
    }

    errcode = process_writePtr(rustsyscallhandler_getProcess(sys), bufPtr, &buf, sizeof(buf));

    return syscallreturn_makeDoneI64(res);
}

SyscallReturn syscallhandler_fstatfs(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr bufPtr = args->args[1].as_ptr; // struct statfs*

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get some memory in which to return the result. */
    struct statfs buf = {0};

    int res = regularfile_fstatfs(file_desc, &buf);
    if (res < 0) {
        syscallreturn_makeDoneErrno(-res);
    }

    errcode = process_writePtr(rustsyscallhandler_getProcess(sys), bufPtr, &buf, sizeof(buf));
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(res);
}

SyscallReturn syscallhandler_fsync(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SyscallReturn syscallhandler_fdatasync(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SyscallReturn syscallhandler_syncfs(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SyscallReturn syscallhandler_fchown(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(
        regularfile_fchown(file_desc, args->args[1].as_u64, args->args[2].as_u64));
}

SyscallReturn syscallhandler_fchmod(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_fchmod(file_desc, args->args[1].as_u64));
}

SyscallReturn syscallhandler_fallocate(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_fallocate(
        file_desc, args->args[1].as_i64, args->args[2].as_u64, args->args[3].as_u64));
}

SyscallReturn syscallhandler_ftruncate(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_ftruncate(file_desc, args->args[1].as_u64));
}

SyscallReturn syscallhandler_fadvise64(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_fadvise(
        file_desc, args->args[1].as_u64, args->args[2].as_u64, args->args[3].as_i64));
}

SyscallReturn syscallhandler_flock(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_flock(file_desc, args->args[1].as_i64));
}

SyscallReturn syscallhandler_fsetxattr(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr namePtr = args->args[1].as_ptr;  // const char*
    UntypedForeignPtr valuePtr = args->args[2].as_ptr; // const void*
    size_t size = args->args[3].as_u64;
    int flags = args->args[4].as_i64;

    int res = 0;
    int errcode = 0;
    char* value = NULL;
    RegularFile* file_desc = NULL;

    /* Get and validate the file descriptor. */
    errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        goto out;
    }

    /* Get the name/value strings from the plugin. */
    char name[PATH_MAX];
    errcode = process_readString(rustsyscallhandler_getProcess(sys), name, namePtr, sizeof(name));
    if (errcode < 0) {
        goto out;
    }

    if (valuePtr.val && size > 0) {
        value = malloc(size);
        if (value == NULL) {
            warning("Internally failed to allocate %lu bytes", size);
            errcode = -ENOMEM;
            goto out;
        }
        errcode = process_readPtr(rustsyscallhandler_getProcess(sys), value, valuePtr, size);
        if (errcode < 0) {
            goto out;
        }
    }

    res = regularfile_fsetxattr(file_desc, name, value, size, flags);

out:
    if (value) {
        free(value);
    }
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    return syscallreturn_makeDoneI64(res);
}

SyscallReturn syscallhandler_fgetxattr(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr namePtr = args->args[1].as_ptr;  // const char*
    UntypedForeignPtr valuePtr = args->args[2].as_ptr; // void*
    size_t size = args->args[3].as_u64;

    ssize_t res = 0;
    ssize_t errcode = 0;
    char* value = NULL;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        goto out;
    }

    /* Get the name/value strings from the plugin. */
    char name[PATH_MAX];
    errcode = process_readString(rustsyscallhandler_getProcess(sys), name, namePtr, sizeof(name));
    if (errcode < 0) {
        goto out;
    }

    if (valuePtr.val && size > 0) {
        value = malloc(size);
        if (value == NULL) {
            warning("Internally failed to allocate %lu bytes", size);
            errcode = -ENOMEM;
            goto out;
        }
    }

    errcode = regularfile_fgetxattr(file_desc, name, value, size);
    if (errcode < 0) {
        goto out;
    }
    res = errcode;

    if (value) {
        // Write back `res` bytes of the result buffer..
        errcode = process_writePtr(rustsyscallhandler_getProcess(sys), valuePtr, value, res);
        if (errcode < 0) {
            goto out;
        }
    }

out:
    if (value) {
        free(value);
    }
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    return syscallreturn_makeDoneI64(res);
}

SyscallReturn syscallhandler_flistxattr(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr listPtr = args->args[1].as_ptr; // char*
    size_t size = args->args[2].as_u64;

    void* list = NULL;
    ssize_t errcode = 0;
    ssize_t res = -1;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        goto out;
    }

    if (listPtr.val && size > 0) {
        list = malloc(size);
        if (list == NULL) {
            warning("Internally failed to allocate %lu bytes", size);
            errcode = -ENOMEM;
            goto out;
        }
    }

    errcode = regularfile_flistxattr(file_desc, list, size);
    if (errcode < 0) {
        goto out;
    }
    res = errcode;

    if (list) {
        // Write back `res` bytes of the list buffer.
        errcode = process_writePtr(rustsyscallhandler_getProcess(sys), listPtr, list, res);
        if (errcode < 0) {
            goto out;
        }
    }

out:
    if (list) {
        free(list);
    }
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    return syscallreturn_makeDoneI64(res);
}

SyscallReturn syscallhandler_fremovexattr(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr namePtr = args->args[1].as_ptr; // const char*

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the name string from the plugin. */
    char name[PATH_MAX];
    errcode = process_readString(rustsyscallhandler_getProcess(sys), name, namePtr, sizeof(name));
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_fremovexattr(file_desc, name));
}

SyscallReturn syscallhandler_sync_file_range(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    off64_t offset = args->args[1].as_u64;
    off64_t nbytes = args->args[2].as_u64;
    unsigned int flags = args->args[3].as_u64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_sync_range(file_desc, offset, nbytes, flags));
}

SyscallReturn syscallhandler_readahead(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    off64_t offset = args->args[1].as_u64;
    size_t count = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_readahead(file_desc, offset, count));
}

SyscallReturn syscallhandler_lseek(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    off_t offset = args->args[1].as_u64;
    int whence = args->args[2].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_lseek(file_desc, offset, whence));
}

SyscallReturn syscallhandler_getdents(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr dirpPtr = args->args[1].as_ptr; // struct linux_dirent*
    unsigned int count = args->args[2].as_u64;

    struct linux_dirent* dirp = NULL;
    ssize_t errcode = 0;
    ssize_t res = -1;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        goto out;
    }

    dirp = malloc(count);
    if (dirp == NULL) {
        warning("Internally failed to allocate %u bytes", count);
        errcode = -ENOMEM;
        goto out;
    }

    errcode = regularfile_getdents(file_desc, dirp, count);
    if (errcode < 0) {
        goto out;
    }
    res = errcode;

    // Write back `res` bytes of the dirp buffer.
    errcode = process_writePtr(rustsyscallhandler_getProcess(sys), dirpPtr, dirp, res);
    if (errcode < 0) {
        goto out;
    }

out:
    if (dirp) {
        free(dirp);
    }
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    return syscallreturn_makeDoneI64(res);
}

SyscallReturn syscallhandler_getdents64(SyscallHandler* sys, const SyscallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr dirpPtr = args->args[1].as_ptr; // struct linux_dirent64*
    unsigned int count = args->args[2].as_u64;

    struct linux_dirent64* dirp = NULL;
    ssize_t errcode = 0;
    ssize_t res = -1;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        goto out;
    }

    dirp = malloc(count);
    if (dirp == NULL) {
        warning("Internally failed to allocate %u bytes", count);
        errcode = -ENOMEM;
        goto out;
    }

    errcode = regularfile_getdents64(file_desc, dirp, count);
    if (errcode < 0) {
        goto out;
    }
    res = errcode;

    // Write back `res` bytes of the `dirp` buffer.
    errcode = process_writePtr(rustsyscallhandler_getProcess(sys), dirpPtr, dirp, res);
    if (errcode < 0) {
        goto out;
    }

out:
    if (dirp) {
        free(dirp);
    }
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }
    return syscallreturn_makeDoneI64(res);
}
