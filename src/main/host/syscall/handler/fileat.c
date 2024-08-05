/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/handler/fileat.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

/* If dirfd is the special value AT_FDCWD, this sets dir_desc_out to NULL
 * and returns 0 to indicate that dirfd is a valid value. */
static int _syscallhandler_validateDirHelper(SyscallHandler* sys, int dirfd,
                                             RegularFile** dir_desc_out) {
    /* Check that fd is within bounds. */
    if (dirfd == AT_FDCWD) {
        if (dir_desc_out) {
            *dir_desc_out = NULL;
        }
        return 0;
    } else if (dirfd < 0) {
        debug("descriptor %i out of bounds", dirfd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    LegacyFile* desc = thread_getRegisteredLegacyFile(rustsyscallhandler_getThread(sys), dirfd);
    if (desc && dir_desc_out) {
        *dir_desc_out = (RegularFile*)desc;
    }

    int errcode = _syscallhandler_validateLegacyFile(desc, DT_FILE);
    if (errcode) {
        debug("descriptor %i is invalid", dirfd);
        return errcode;
    }

    /* Now we know we have a valid file. */
    return 0;
}

static int _syscallhandler_validateDirAndPathnameHelper(SyscallHandler* sys, int dirfd,
                                                        UntypedForeignPtr pathnamePtr,
                                                        RegularFile** dir_desc_out,
                                                        const char** pathname_out) {
    /* Validate the directory fd. */
    RegularFile* dir_desc = NULL;
    int errcode = _syscallhandler_validateDirHelper(sys, dirfd, dir_desc_out);
    if (errcode < 0) {
        return errcode;
    }

    /* Get the path string from the plugin. */
    return process_getReadableString(
        rustsyscallhandler_getProcess(sys), pathnamePtr, PATH_MAX, pathname_out, NULL);
}

static SyscallReturn _syscallhandler_renameatHelper(SyscallHandler* sys, int olddirfd,
                                                    UntypedForeignPtr oldpathPtr, int newdirfd,
                                                    UntypedForeignPtr newpathPtr,
                                                    unsigned int flags) {
    /* Validate params. */
    RegularFile* olddir_desc = NULL;
    const char* oldpath;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, olddirfd, oldpathPtr, &olddir_desc, &oldpath);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    RegularFile* newdir_desc = NULL;
    const char* newpath;

    errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, newdirfd, newpathPtr, &newdir_desc, &newpath);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_renameat2(olddir_desc, oldpath, newdir_desc, newpath, flags, plugin_cwd));
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_openat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int flags = args->args[2].as_i64;
    mode_t mode = args->args[3].as_u64;

    trace("Trying to openat file with path name at plugin addr %p",
          (void*)pathnamePtr.val);

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Create and open the file. */
    RegularFile* file_desc = regularfile_new();
    errcode = regularfile_openat(file_desc, dir_desc, pathname, flags & ~O_CLOEXEC, mode,
                                 process_getWorkingDir(rustsyscallhandler_getProcess(sys)));

    if (errcode < 0) {
        /* This will unref/free the RegularFile. */
        legacyfile_close((LegacyFile*)file_desc, rustsyscallhandler_getHost(sys));
        legacyfile_unref(file_desc);
        return syscallreturn_makeDoneErrno(-errcode);
    }

    utility_debugAssert(errcode == 0);
    Descriptor* desc = descriptor_fromLegacyFile((LegacyFile*)file_desc, flags & O_CLOEXEC);
    int handle = thread_registerDescriptor(rustsyscallhandler_getThread(sys), desc);
    return syscallreturn_makeDoneI64(handle);
}

SyscallReturn syscallhandler_newfstatat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    UntypedForeignPtr bufPtr = args->args[2].as_ptr;      // struct stat*
    int flags = args->args[3].as_i64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;

    ssize_t errcode = _syscallhandler_validateDirHelper(sys, dirfd, &dir_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Copy the path rather than getting a reference, so that the MemoryManager
     * will still allow us to get a mutable reference to memory below.
     */
    char pathname[PATH_MAX];
    errcode = process_readString(rustsyscallhandler_getProcess(sys), pathname, pathnamePtr, PATH_MAX);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get some memory in which to return the result. */
    struct stat* buf =
        process_getWriteablePtr(rustsyscallhandler_getProcess(sys), bufPtr, sizeof(*buf));
    if (!buf) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_fstatat(dir_desc, pathname, buf, flags, plugin_cwd));
}

SyscallReturn syscallhandler_fchownat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    uid_t owner = args->args[2].as_u64;
    gid_t group = args->args[3].as_u64;
    int flags = args->args[4].as_i64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_fchownat(dir_desc, pathname, owner, group, flags, plugin_cwd));
}

SyscallReturn syscallhandler_fchmodat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    uid_t mode = args->args[2].as_u64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_fchmodat(dir_desc, pathname, mode, 0, plugin_cwd));
}

SyscallReturn syscallhandler_fchmodat2(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    uid_t mode = args->args[2].as_u64;
    int flags = args->args[3].as_i64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_fchmodat(dir_desc, pathname, mode, flags, plugin_cwd));
}

SyscallReturn syscallhandler_futimesat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    UntypedForeignPtr timesPtr = args->args[2].as_ptr;    // const struct timeval [2]

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const struct timeval* times =
        process_getReadablePtr(rustsyscallhandler_getProcess(sys), timesPtr, 2 * sizeof(*times));
    if (!times) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(regularfile_futimesat(dir_desc, pathname, times, plugin_cwd));
}

SyscallReturn syscallhandler_utimensat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    UntypedForeignPtr timesPtr = args->args[2].as_ptr;    // const struct timespec [2]
    int flags = args->args[3].as_i64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const struct timespec* times =
        process_getReadablePtr(rustsyscallhandler_getProcess(sys), timesPtr, 2 * sizeof(*times));
    if (!times) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_utimensat(dir_desc, pathname, times, flags, plugin_cwd));
}

SyscallReturn syscallhandler_faccessat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int mode = args->args[2].as_i64;
    int flags = args->args[3].as_i64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_faccessat(dir_desc, pathname, mode, flags, plugin_cwd));
}

SyscallReturn syscallhandler_mkdirat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    mode_t mode = args->args[2].as_u64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(regularfile_mkdirat(dir_desc, pathname, mode, plugin_cwd));
}

SyscallReturn syscallhandler_mknodat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    mode_t mode = args->args[2].as_u64;
    dev_t dev = args->args[3].as_u64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_mknodat(dir_desc, pathname, mode, dev, plugin_cwd));
}

SyscallReturn syscallhandler_linkat(SyscallHandler* sys, const SyscallArgs* args) {
    int olddirfd = args->args[0].as_i64;
    UntypedForeignPtr oldpathPtr = args->args[1].as_ptr; // const char*
    int newdirfd = args->args[2].as_i64;
    UntypedForeignPtr newpathPtr = args->args[3].as_ptr; // const char*
    int flags = args->args[4].as_i64;

    /* Validate params. */
    RegularFile* olddir_desc = NULL;
    const char* oldpath;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, olddirfd, oldpathPtr, &olddir_desc, &oldpath);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    RegularFile* newdir_desc = NULL;
    const char* newpath;

    errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, newdirfd, newpathPtr, &newdir_desc, &newpath);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_linkat(olddir_desc, oldpath, newdir_desc, newpath, flags, plugin_cwd));
}

SyscallReturn syscallhandler_unlinkat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int flags = args->args[2].as_i64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(regularfile_unlinkat(dir_desc, pathname, flags, plugin_cwd));
}

SyscallReturn syscallhandler_symlinkat(SyscallHandler* sys, const SyscallArgs* args) {
    UntypedForeignPtr targetpathPtr = args->args[0].as_ptr; // const char*
    int dirfd = args->args[1].as_i64;
    UntypedForeignPtr linkpathPtr = args->args[2].as_ptr; // const char*

    /* Validate params. */
    RegularFile* dir_desc = NULL;
    const char* linkpath;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, linkpathPtr, &dir_desc, &linkpath);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the path string from the plugin. */
    const char* targetpath;
    errcode = process_getReadableString(
        rustsyscallhandler_getProcess(sys), targetpathPtr, PATH_MAX, &targetpath, NULL);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_symlinkat(dir_desc, linkpath, targetpath, plugin_cwd));
}

SyscallReturn syscallhandler_readlinkat(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    UntypedForeignPtr bufPtr = args->args[2].as_ptr;      // char*
    size_t bufSize = args->args[3].as_u64;

    /* Validate params. */
    RegularFile* dir_desc = NULL;

    ssize_t errcode = _syscallhandler_validateDirHelper(sys, dirfd, &dir_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Copy the path rather than getting a reference, so that the MemoryManager
     * will still allow us to get a mutable reference to memory below.
     */
    char pathname[PATH_MAX];
    errcode = process_readString(rustsyscallhandler_getProcess(sys), pathname, pathnamePtr, PATH_MAX);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the path string from the plugin. */
    char* buf = process_getWriteablePtr(rustsyscallhandler_getProcess(sys), bufPtr, bufSize);
    if (!buf) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_readlinkat(dir_desc, pathname, buf, bufSize, plugin_cwd));
}

SyscallReturn syscallhandler_renameat(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_renameatHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_i64,
        args->args[3].as_ptr, 0);
}

SyscallReturn syscallhandler_renameat2(SyscallHandler* sys, const SyscallArgs* args) {
    return _syscallhandler_renameatHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_i64,
        args->args[3].as_ptr, args->args[4].as_u64);
}

#ifdef SYS_statx
SyscallReturn syscallhandler_statx(SyscallHandler* sys, const SyscallArgs* args) {
    int dirfd = args->args[0].as_i64;
    UntypedForeignPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int flags = args->args[2].as_i64;
    unsigned int mask = args->args[3].as_u64;
    UntypedForeignPtr statxbufPtr = args->args[4].as_ptr; // struct statx*

    /* Validate params. */
    RegularFile* dir_desc = NULL;

    ssize_t errcode = _syscallhandler_validateDirHelper(sys, dirfd, &dir_desc);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Copy the path rather than getting a reference, so that the MemoryManager
     * will still allow us to get a mutable reference to memory below.
     */
    char pathname[PATH_MAX];
    errcode = process_readString(rustsyscallhandler_getProcess(sys), pathname, pathnamePtr, PATH_MAX);
    if (errcode < 0) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Get the path string from the plugin. */
    struct statx* statxbuf =
        process_getWriteablePtr(rustsyscallhandler_getProcess(sys), statxbufPtr, sizeof(*statxbuf));
    if (!statxbuf) {
        return syscallreturn_makeDoneErrno(EFAULT);
    }

    const char* plugin_cwd = process_getWorkingDir(rustsyscallhandler_getProcess(sys));

    return syscallreturn_makeDoneI64(
        regularfile_statx(dir_desc, pathname, flags, mask, statxbuf, plugin_cwd));
}
#endif
