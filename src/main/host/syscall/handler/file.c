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
    const char* pathname;
    int errcode = process_getReadableString(
        rustsyscallhandler_getProcess(sys), pathnamePtr, PATH_MAX, &pathname, NULL);
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

SyscallReturn syscallhandler_creat(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_openHelper(sys, args->args[0].as_ptr,
                                      O_CREAT | O_WRONLY | O_TRUNC,
                                      args->args[1].as_u64);
}

SyscallReturn syscallhandler_open(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_openHelper(
        sys, args->args[0].as_ptr, args->args[1].as_i64, args->args[2].as_u64);
}

SyscallReturn syscallhandler_fstat(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr bufPtr = args->args[1].as_ptr; // struct stat*

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get some memory in which to return the result. */
    struct stat* buf =
        process_getWriteablePtr(rustsyscallhandler_getProcess(sys), bufPtr, sizeof(*buf));
    if (!buf) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    return syscallreturn_makeDoneI64(regularfile_fstat(file_desc, buf));
}

SyscallReturn syscallhandler_fstatfs(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr bufPtr = args->args[1].as_ptr; // struct statfs*

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get some memory in which to return the result. */
    struct statfs* buf =
        process_getWriteablePtr(rustsyscallhandler_getProcess(sys), bufPtr, sizeof(*buf));
    if (!buf) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    return syscallreturn_makeDoneI64(regularfile_fstatfs(file_desc, buf));
}

SyscallReturn syscallhandler_fsync(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SyscallReturn syscallhandler_fdatasync(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SyscallReturn syscallhandler_syncfs(SyscallHandler* sys, const SysCallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SyscallReturn syscallhandler_fchown(SyscallHandler* sys, const SysCallArgs* args) {
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

SyscallReturn syscallhandler_fchmod(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_fchmod(file_desc, args->args[1].as_u64));
}

SyscallReturn syscallhandler_fallocate(SyscallHandler* sys, const SysCallArgs* args) {
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

SyscallReturn syscallhandler_ftruncate(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_ftruncate(file_desc, args->args[1].as_u64));
}

SyscallReturn syscallhandler_fadvise64(SyscallHandler* sys, const SysCallArgs* args) {
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

SyscallReturn syscallhandler_flock(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_flock(file_desc, args->args[1].as_i64));
}

SyscallReturn syscallhandler_fsetxattr(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr namePtr = args->args[1].as_ptr;  // const char*
    UntypedForeignPtr valuePtr = args->args[2].as_ptr; // const void*
    size_t size = args->args[3].as_u64;
    int flags = args->args[4].as_i64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the name/value strings from the plugin. */
    const char* name;
    errcode =
        process_getReadableString(rustsyscallhandler_getProcess(sys), namePtr, PATH_MAX, &name, NULL);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const void* value =
        (valuePtr.val && size > 0)
            ? process_getReadablePtr(rustsyscallhandler_getProcess(sys), valuePtr, size)
            : NULL;

    return syscallreturn_makeDoneI64(regularfile_fsetxattr(file_desc, name, value, size, flags));
}

SyscallReturn syscallhandler_fgetxattr(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr namePtr = args->args[1].as_ptr;  // const char*
    UntypedForeignPtr valuePtr = args->args[2].as_ptr; // void*
    size_t size = args->args[3].as_u64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the name/value strings from the plugin. */
    const char* name;
    errcode =
        process_getReadableString(rustsyscallhandler_getProcess(sys), namePtr, PATH_MAX, &name, NULL);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    void* value = (valuePtr.val && size > 0)
                      ? process_getWriteablePtr(rustsyscallhandler_getProcess(sys), valuePtr, size)
                      : NULL;

    return syscallreturn_makeDoneI64(regularfile_fgetxattr(file_desc, name, value, size));
}

SyscallReturn syscallhandler_flistxattr(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr listPtr = args->args[1].as_ptr; // char*
    size_t size = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    void* list = (listPtr.val && size > 0)
                     ? process_getWriteablePtr(rustsyscallhandler_getProcess(sys), listPtr, size)
                     : NULL;

    return syscallreturn_makeDoneI64(regularfile_flistxattr(file_desc, list, size));
}

SyscallReturn syscallhandler_fremovexattr(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr namePtr = args->args[1].as_ptr; // const char*

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the name string from the plugin. */
    const char* name;
    errcode =
        process_getReadableString(rustsyscallhandler_getProcess(sys), namePtr, PATH_MAX, &name, NULL);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    return syscallreturn_makeDoneI64(regularfile_fremovexattr(file_desc, name));
}

SyscallReturn syscallhandler_sync_file_range(SyscallHandler* sys, const SysCallArgs* args) {
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

SyscallReturn syscallhandler_readahead(SyscallHandler* sys, const SysCallArgs* args) {
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

SyscallReturn syscallhandler_lseek(SyscallHandler* sys, const SysCallArgs* args) {
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

SyscallReturn syscallhandler_getdents(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr dirpPtr = args->args[1].as_ptr; // struct linux_dirent*
    unsigned int count = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the path string from the plugin. */
    struct linux_dirent* dirp =
        process_getWriteablePtr(rustsyscallhandler_getProcess(sys), dirpPtr, count);
    if (!dirp) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    return syscallreturn_makeDoneI64(regularfile_getdents(file_desc, dirp, count));
}

SyscallReturn syscallhandler_getdents64(SyscallHandler* sys, const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    UntypedForeignPtr dirpPtr = args->args[1].as_ptr; // struct linux_dirent64*
    unsigned int count = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the path string from the plugin. */
    struct linux_dirent64* dirp =
        process_getWriteablePtr(rustsyscallhandler_getProcess(sys), dirpPtr, count);
    if (!dirp) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    return syscallreturn_makeDoneI64(regularfile_getdents64(file_desc, dirp, count));
}
