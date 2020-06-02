/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/file.h"
#include "main/host/syscall/protected.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateFileHelper(SysCallHandler* sys, int filefd,
                                                File** file_desc_out) {
    /* Check that fd is within bounds. */
    if (filefd <= 0) {
        info("descriptor %i out of bounds", filefd);
        return -EBADF;
    }

    /* Check if this is a virtual Shadow descriptor. */
    Descriptor* desc = host_lookupDescriptor(sys->host, filefd);
    if (desc && file_desc_out) {
        *file_desc_out = (File*)desc;
    }

    int errcode = _syscallhandler_validateDescriptor(desc, DT_FILE);
    if (errcode) {
        info("descriptor %i is invalid", filefd);
        return errcode;
    }

    /* Now we know we have a valid file. */
    return 0;
}

static SysCallReturn _syscallhandler_openHelper(SysCallHandler* sys, int dirfd, PluginPtr pathnamePtr, int flags, mode_t mode) {
    debug("Trying to open file at dir %i with path name at %p", dirfd, pathnamePtr.val);

    /* Get and validate the directory file descriptor. */
    File* dir_desc = NULL;
    int errcode = 0;

    if(dirfd > 0) {
        errcode = _syscallhandler_validateFileHelper(sys, dirfd, &dir_desc);
    }
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Create the new descriptor for this file. */
    Descriptor* desc = host_createDescriptor(sys->host, DT_FILE);
    utility_assert(desc);
    utility_assert(_syscallhandler_validateDescriptor(desc, DT_FILE) == 0);

    const char* pathname;
    if(pathnamePtr.val) {
        pathname = thread_getReadablePtr(sys->thread, pathnamePtr, PATH_MAX);
    } else {
        pathname = NULL;
    }

    errcode = file_open((File*)desc, dir_desc, pathname, flags, mode);
    if (errcode < 0) {
        /* This will remove the descriptor entry and unref/free the File. */
        descriptor_close(desc);
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
}

static SysCallReturn _syscallhandler_fsyncHelper(SysCallHandler* sys, int fd) {
    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fsync(file_desc)};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_creat(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    return _syscallhandler_openHelper(sys, 0, args->args[0].as_ptr, O_CREAT|O_WRONLY|O_TRUNC, args->args[1].as_u64);
}

SysCallReturn syscallhandler_open(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    return _syscallhandler_openHelper(sys, 0, args->args[0].as_ptr, args->args[1].as_i64, args->args[2].as_u64);
}

SysCallReturn syscallhandler_openat(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    return _syscallhandler_openHelper(sys, args->args[0].as_i64, args->args[1].as_ptr, args->args[2].as_i64, args->args[3].as_u64);
}

SysCallReturn syscallhandler_fstat(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr bufPtr = args->args[1].as_ptr; // struct stat*

    /* Check that the buffer is not NULL */
    if(!bufPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get some memory in which to return the result. */
    struct stat* buf = thread_getWriteablePtr(sys->thread, bufPtr, sizeof(*buf));

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fstat(file_desc, buf)};
}

SysCallReturn syscallhandler_fstatfs(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr bufPtr = args->args[1].as_ptr; // struct statfs*

    /* Check that the buffer is not NULL */
    if(!bufPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get some memory in which to return the result. */
    struct statfs* buf = thread_getWriteablePtr(sys->thread, bufPtr, sizeof(*buf));

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fstatfs(file_desc, buf)};
}

SysCallReturn syscallhandler_newfstatat(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int dirfd = args->args[0].as_i64;
    PluginPtr pathnamePtr = args->args[1].as_ptr; // const char*
    PluginPtr bufPtr = args->args[2].as_ptr; // struct stat*
    int flags = args->args[3].as_i64;

    /* Check that the buffer and pathname are not NULL */
    if(!bufPtr.val || !pathnamePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the directory file descriptor. */

    if(dirfd > 0) {
        File* dir_desc = NULL;
        int errcode = _syscallhandler_validateFileHelper(sys, dirfd, &dir_desc);
        if (errcode < 0) {
            return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
        } else {
            dirfd = file_getOSBackedFD(dir_desc);
        }
    }

    /* Read the pathname string. */
    const char* pathname = thread_getReadablePtr(sys->thread, bufPtr, PATH_MAX);

    /* Get some memory in which to return the result. */
    struct stat* buf = thread_getWriteablePtr(sys->thread, bufPtr, sizeof(*buf));

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = fstatat(dirfd, pathname, buf, flags)};
}

SysCallReturn syscallhandler_fsync(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SysCallReturn syscallhandler_fdatasync(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SysCallReturn syscallhandler_syncfs(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    return _syscallhandler_fsyncHelper(sys, args->args[0].as_i64);
}

SysCallReturn syscallhandler_fchown(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fchown(file_desc, args->args[1].as_u64, args->args[2].as_u64)};
}

SysCallReturn syscallhandler_fchmod(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fchmod(file_desc, args->args[1].as_u64)};
}

SysCallReturn syscallhandler_fchdir(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fchdir(file_desc)};
}

SysCallReturn syscallhandler_fallocate(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fallocate(file_desc, args->args[1].as_i64, args->args[2].as_u64, args->args[3].as_u64)};
}

SysCallReturn syscallhandler_ftruncate(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_ftruncate(file_desc, args->args[1].as_u64)};
}

SysCallReturn syscallhandler_fadvise64(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fadvise(file_desc, args->args[1].as_u64, args->args[2].as_u64, args->args[3].as_i64)};
}

SysCallReturn syscallhandler_flock(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_flock(file_desc, args->args[1].as_i64)};
}

SysCallReturn syscallhandler_fsetxattr(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr namePtr = args->args[1].as_ptr; // const char*
    PluginPtr valuePtr = args->args[2].as_ptr; // const void*
    size_t size = args->args[3].as_u64;
    int flags = args->args[4].as_i64;

    if(!namePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -ENOTSUP};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* name = thread_getReadablePtr(sys->thread, namePtr, PATH_MAX);
    const void* value = (valuePtr.val && size > 0) ? thread_getReadablePtr(sys->thread, valuePtr, size) : NULL;

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fsetxattr(file_desc, name, value, size, flags)};
}

SysCallReturn syscallhandler_fgetxattr(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr namePtr = args->args[1].as_ptr; // const char*
    PluginPtr valuePtr = args->args[2].as_ptr; // void*
    size_t size = args->args[3].as_u64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* name = thread_getReadablePtr(sys->thread, namePtr, PATH_MAX);
    void* value = (valuePtr.val && size > 0) ? thread_getWriteablePtr(sys->thread, valuePtr, size) : NULL;

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fgetxattr(file_desc, name, value, size)};
}

SysCallReturn syscallhandler_flistxattr(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr listPtr = args->args[1].as_ptr; // char*
    size_t size = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    void* list = (listPtr.val && size > 0) ? thread_getWriteablePtr(sys->thread, listPtr, size) : NULL;

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_flistxattr(file_desc, list, size)};
}

SysCallReturn syscallhandler_fremovexattr(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr namePtr = args->args[1].as_ptr; // const char*

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const char* name = thread_getReadablePtr(sys->thread, namePtr, PATH_MAX);

    return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = file_fremovexattr(file_desc, name)};
}
