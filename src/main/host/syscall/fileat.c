/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/fileat.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/file.h"
#include "main/host/process.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall/protected.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

/* If dirfd is the special value AT_FDCWD, this sets dir_desc_out to NULL
 * and returns 0 to indicate that dirfd is a valid value. */
static int _syscallhandler_validateDirHelper(SysCallHandler* sys, int dirfd,
                                             File** dir_desc_out) {
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
    LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, dirfd);
    if (desc && dir_desc_out) {
        *dir_desc_out = (File*)desc;
    }

    int errcode = _syscallhandler_validateDescriptor(desc, DT_FILE);
    if (errcode) {
        debug("descriptor %i is invalid", dirfd);
        return errcode;
    }

    /* Now we know we have a valid file. */
    return 0;
}

static int _syscallhandler_validateDirAndPathnameHelper(
    SysCallHandler* sys, int dirfd, PluginPtr pathnamePtr, File** dir_desc_out,
    const char** pathname_out) {
    /* Validate the directory fd. */
    File* dir_desc = NULL;
    int errcode = _syscallhandler_validateDirHelper(sys, dirfd, dir_desc_out);
    if (errcode < 0) {
        return errcode;
    }

    /* Get the path string from the plugin. */
    return process_getReadableString(sys->process, pathnamePtr, PATH_MAX, pathname_out, NULL);
}

static SysCallReturn
_syscallhandler_renameatHelper(SysCallHandler* sys, int olddirfd,
                               PluginPtr oldpathPtr, int newdirfd,
                               PluginPtr newpathPtr, unsigned int flags) {
    /* Validate params. */
    File* olddir_desc = NULL;
    const char* oldpath;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, olddirfd, oldpathPtr, &olddir_desc, &oldpath);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    File* newdir_desc = NULL;
    const char* newpath;

    errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, newdirfd, newpathPtr, &newdir_desc, &newpath);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 =
            file_renameat2(olddir_desc, oldpath, newdir_desc, newpath, flags, plugin_cwd)};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_openat(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int flags = args->args[2].as_i64;
    mode_t mode = args->args[3].as_u64;

    trace("Trying to openat file with path name at plugin addr %p",
          (void*)pathnamePtr.val);

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Create the new descriptor for this file. */
    File* file_desc = file_new();
    int handle =
        process_registerLegacyDescriptor(sys->process, (LegacyDescriptor*)file_desc);

    /* Now open the file. */
    errcode = file_openat(file_desc, dir_desc, pathname, flags, mode, process_getWorkingDir(sys->process));
    if (errcode < 0) {
        /* This will remove the descriptor entry and unref/free the File. */
        descriptor_close((LegacyDescriptor*)file_desc, sys->host);
    } else {
        utility_assert(errcode == handle);
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
}

SysCallReturn syscallhandler_newfstatat(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    PluginPtr bufPtr = args->args[2].as_ptr;      // struct stat*
    int flags = args->args[3].as_i64;

    /* Validate params. */
    File* dir_desc = NULL;

    ssize_t errcode = _syscallhandler_validateDirHelper(sys, dirfd, &dir_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Copy the path rather than getting a reference, so that the MemoryManager
     * will still allow us to get a mutable reference to memory below.
     */
    char pathname[PATH_MAX];
    errcode = process_readString(sys->process, pathname, pathnamePtr, PATH_MAX);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Check for non-NULL buffer. */
    if (!bufPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get some memory in which to return the result. */
    struct stat* buf = process_getWriteablePtr(sys->process, bufPtr, sizeof(*buf));

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_fstatat(dir_desc, pathname, buf, flags, plugin_cwd)};
}

SysCallReturn syscallhandler_fchownat(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    uid_t owner = args->args[2].as_u64;
    gid_t group = args->args[3].as_u64;
    int flags = args->args[4].as_i64;

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){.state = SYSCALL_DONE,
                           .retval.as_i64 = file_fchownat(
                               dir_desc, pathname, owner, group, flags, plugin_cwd)};
}

SysCallReturn syscallhandler_fchmodat(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    uid_t mode = args->args[2].as_u64;
    int flags = args->args[3].as_i64;

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_fchmodat(dir_desc, pathname, mode, flags, plugin_cwd)};
}

SysCallReturn syscallhandler_futimesat(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    PluginPtr timesPtr = args->args[2].as_ptr;    // const struct timeval [2]

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const struct timeval* times =
        process_getReadablePtr(sys->process, timesPtr, 2 * sizeof(*times));
    if (!times) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_futimesat(dir_desc, pathname, times, plugin_cwd)};
}

SysCallReturn syscallhandler_utimensat(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    PluginPtr timesPtr = args->args[2].as_ptr;    // const struct timespec [2]
    int flags = args->args[3].as_i64;

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const struct timespec* times =
        process_getReadablePtr(sys->process, timesPtr, 2 * sizeof(*times));
    if (!times) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_utimensat(dir_desc, pathname, times, flags, plugin_cwd)};
}

SysCallReturn syscallhandler_faccessat(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int mode = args->args[2].as_i64;
    int flags = args->args[3].as_i64;

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_faccessat(dir_desc, pathname, mode, flags, plugin_cwd)};
}

SysCallReturn syscallhandler_mkdirat(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    mode_t mode = args->args[2].as_u64;

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_mkdirat(dir_desc, pathname, mode, plugin_cwd)};
}

SysCallReturn syscallhandler_mknodat(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    mode_t mode = args->args[2].as_u64;
    dev_t dev = args->args[3].as_u64;

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_mknodat(dir_desc, pathname, mode, dev, plugin_cwd)};
}

SysCallReturn syscallhandler_linkat(SysCallHandler* sys,
                                    const SysCallArgs* args) {
    int olddirfd = args->args[0].as_i64;
    PluginPtr oldpathPtr = args->args[1].as_ptr; // const char*
    int newdirfd = args->args[2].as_i64;
    PluginPtr newpathPtr = args->args[3].as_ptr; // const char*
    int flags = args->args[4].as_i64;

    /* Validate params. */
    File* olddir_desc = NULL;
    const char* oldpath;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, olddirfd, oldpathPtr, &olddir_desc, &oldpath);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    File* newdir_desc = NULL;
    const char* newpath;

    errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, newdirfd, newpathPtr, &newdir_desc, &newpath);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 =
            file_linkat(olddir_desc, oldpath, newdir_desc, newpath, flags, plugin_cwd)};
}

SysCallReturn syscallhandler_unlinkat(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int flags = args->args[2].as_i64;

    /* Validate params. */
    File* dir_desc = NULL;
    const char* pathname;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, pathnamePtr, &dir_desc, &pathname);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_unlinkat(dir_desc, pathname, flags, plugin_cwd)};
}

SysCallReturn syscallhandler_symlinkat(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    PluginPtr targetpathPtr = args->args[0].as_ptr; // const char*
    int dirfd = args->args[1].as_i64;
    PluginPtr linkpathPtr = args->args[2].as_ptr; // const char*

    /* Validate params. */
    File* dir_desc = NULL;
    const char* linkpath;

    int errcode = _syscallhandler_validateDirAndPathnameHelper(
        sys, dirfd, linkpathPtr, &dir_desc, &linkpath);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get the path string from the plugin. */
    const char* targetpath;
    errcode = process_getReadableString(sys->process, targetpathPtr, PATH_MAX, &targetpath, NULL);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_symlinkat(dir_desc, linkpath, targetpath, plugin_cwd)};
}

SysCallReturn syscallhandler_readlinkat(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    PluginPtr bufPtr = args->args[2].as_ptr;      // char*
    size_t bufSize = args->args[3].as_u64;

    /* Validate params. */
    File* dir_desc = NULL;

    ssize_t errcode = _syscallhandler_validateDirHelper(sys, dirfd, &dir_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Copy the path rather than getting a reference, so that the MemoryManager
     * will still allow us to get a mutable reference to memory below.
     */
    char pathname[PATH_MAX];
    errcode = process_readString(sys->process, pathname, pathnamePtr, PATH_MAX);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get the path string from the plugin. */
    char* buf = process_getWriteablePtr(sys->process, bufPtr, bufSize);
    if (!buf) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_readlinkat(dir_desc, pathname, buf, bufSize, plugin_cwd)};
}

SysCallReturn syscallhandler_renameat(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    return _syscallhandler_renameatHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_i64,
        args->args[3].as_ptr, 0);
}

SysCallReturn syscallhandler_renameat2(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    return _syscallhandler_renameatHelper(
        sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_i64,
        args->args[3].as_ptr, args->args[4].as_u64);
}

#ifdef SYS_statx
SysCallReturn syscallhandler_statx(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    int flags = args->args[2].as_i64;
    unsigned int mask = args->args[3].as_u64;
    PluginPtr statxbufPtr = args->args[4].as_ptr; // struct statx*

    /* Validate params. */
    File* dir_desc = NULL;

    ssize_t errcode = _syscallhandler_validateDirHelper(sys, dirfd, &dir_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Copy the path rather than getting a reference, so that the MemoryManager
     * will still allow us to get a mutable reference to memory below.
     */
    char pathname[PATH_MAX];
    errcode = process_readString(sys->process, pathname, pathnamePtr, PATH_MAX);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get the path string from the plugin. */
    struct statx* statxbuf = process_getWriteablePtr(sys->process, statxbufPtr, sizeof(*statxbuf));
    if (!statxbuf) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    const char* plugin_cwd = process_getWorkingDir(sys->process);

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_statx(dir_desc, pathname, flags, mask, statxbuf, plugin_cwd)};
}
#endif
