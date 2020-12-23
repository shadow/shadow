/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/file.h"
#include "main/host/process.h"
#include "main/host/syscall/kernel_types.h"
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
    LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, filefd);
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

static SysCallReturn _syscallhandler_openHelper(SysCallHandler* sys,
                                                PluginPtr pathnamePtr,
                                                int flags, mode_t mode) {
    debug("Trying to open file with path name at plugin addr %p",
          (void*)pathnamePtr.val);

    if (!pathnamePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get the path string from the plugin. */
    const char* pathname;
    int errcode = thread_getReadableString(
        sys->thread, pathnamePtr, PATH_MAX, &pathname, NULL);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Create the new descriptor for this file. */
    File* filed = file_new();
    int handle = process_registerLegacyDescriptor(sys->process, (LegacyDescriptor*)filed);

    /* Now open the file. */
    errcode = file_open(filed, pathname, flags, mode);
    if (errcode < 0) {
        /* This will remove the descriptor entry and unref/free the File. */
        descriptor_close((LegacyDescriptor*)filed);
    } else {
        utility_assert(errcode == handle);
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

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = file_fsync(file_desc)};
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_creat(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    return _syscallhandler_openHelper(sys, args->args[0].as_ptr,
                                      O_CREAT | O_WRONLY | O_TRUNC,
                                      args->args[1].as_u64);
}

SysCallReturn syscallhandler_open(SysCallHandler* sys,
                                  const SysCallArgs* args) {
    return _syscallhandler_openHelper(
        sys, args->args[0].as_ptr, args->args[1].as_i64, args->args[2].as_u64);
}

SysCallReturn syscallhandler_fstat(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr bufPtr = args->args[1].as_ptr; // struct stat*

    /* Check that the buffer is not NULL */
    if (!bufPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get some memory in which to return the result. */
    struct stat* buf = process_getWriteablePtr(sys->process, sys->thread, bufPtr, sizeof(*buf));

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = file_fstat(file_desc, buf)};
}

SysCallReturn syscallhandler_fstatfs(SysCallHandler* sys,
                                     const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr bufPtr = args->args[1].as_ptr; // struct statfs*

    /* Check that the buffer is not NULL */
    if (!bufPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get some memory in which to return the result. */
    struct statfs* buf = process_getWriteablePtr(sys->process, sys->thread, bufPtr, sizeof(*buf));

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = file_fstatfs(file_desc, buf)};
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

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 =
            file_fchown(file_desc, args->args[1].as_u64, args->args[2].as_u64)};
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

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_fchmod(file_desc, args->args[1].as_u64)};
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

    return (SysCallReturn){
        .state = SYSCALL_DONE, .retval.as_i64 = file_fchdir(file_desc)};
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

    return (SysCallReturn){.state = SYSCALL_DONE,
                           .retval.as_i64 = file_fallocate(
                               file_desc, args->args[1].as_i64,
                               args->args[2].as_u64, args->args[3].as_u64)};
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

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_ftruncate(file_desc, args->args[1].as_u64)};
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

    return (SysCallReturn){.state = SYSCALL_DONE,
                           .retval.as_i64 = file_fadvise(
                               file_desc, args->args[1].as_u64,
                               args->args[2].as_u64, args->args[3].as_i64)};
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

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_flock(file_desc, args->args[1].as_i64)};
}

SysCallReturn syscallhandler_fsetxattr(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr namePtr = args->args[1].as_ptr;  // const char*
    PluginPtr valuePtr = args->args[2].as_ptr; // const void*
    size_t size = args->args[3].as_u64;
    int flags = args->args[4].as_i64;

    if (!namePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get the name/value strings from the plugin. */
    const char* name;
    errcode =
        thread_getReadableString(sys->thread, namePtr, PATH_MAX, &name, NULL);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    const void* value = (valuePtr.val && size > 0)
                            ? process_getReadablePtr(sys->process, sys->thread, valuePtr, size)
                            : NULL;

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_fsetxattr(file_desc, name, value, size, flags)};
}

SysCallReturn syscallhandler_fgetxattr(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr namePtr = args->args[1].as_ptr;  // const char*
    PluginPtr valuePtr = args->args[2].as_ptr; // void*
    size_t size = args->args[3].as_u64;

    if (!namePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get the name/value strings from the plugin. */
    const char* name;
    errcode =
        thread_getReadableString(sys->thread, namePtr, PATH_MAX, &name, NULL);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    void* value = (valuePtr.val && size > 0)
                      ? process_getWriteablePtr(sys->process, sys->thread, valuePtr, size)
                      : NULL;

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_fgetxattr(file_desc, name, value, size)};
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

    void* list = (listPtr.val && size > 0)
                     ? process_getWriteablePtr(sys->process, sys->thread, listPtr, size)
                     : NULL;

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_flistxattr(file_desc, list, size)};
}

SysCallReturn syscallhandler_fremovexattr(SysCallHandler* sys,
                                          const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    PluginPtr namePtr = args->args[1].as_ptr; // const char*

    if (!namePtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Get the name string from the plugin. */
    const char* name;
    errcode =
        thread_getReadableString(sys->thread, namePtr, PATH_MAX, &name, NULL);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){.state = SYSCALL_DONE,
                           .retval.as_i64 = file_fremovexattr(file_desc, name)};
}

SysCallReturn syscallhandler_sync_file_range(SysCallHandler* sys,
                                             const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    off64_t offset = args->args[1].as_u64;
    off64_t nbytes = args->args[2].as_u64;
    unsigned int flags = args->args[3].as_u64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_sync_range(file_desc, offset, nbytes, flags)};
}

SysCallReturn syscallhandler_readahead(SysCallHandler* sys,
                                       const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    off64_t offset = args->args[1].as_u64;
    size_t count = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_readahead(file_desc, offset, count)};
}

SysCallReturn syscallhandler_lseek(SysCallHandler* sys,
                                   const SysCallArgs* args) {
    int fd = args->args[0].as_i64;
    off_t offset = args->args[1].as_u64;
    int whence = args->args[2].as_i64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_lseek(file_desc, offset, whence)};
}

SysCallReturn syscallhandler_getdents(SysCallHandler* sys,
                                      const SysCallArgs* args) {
    unsigned int fd = args->args[0].as_u64;
    PluginPtr dirpPtr = args->args[1].as_ptr; // struct linux_dirent*
    unsigned int count = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Path should be non-NULL. */
    if (!dirpPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get the path string from the plugin. */
    struct linux_dirent* dirp =
        process_getWriteablePtr(sys->process, sys->thread, dirpPtr, sizeof(*dirp));
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_getdents(file_desc, dirp, count)};
}

SysCallReturn syscallhandler_getdents64(SysCallHandler* sys,
                                        const SysCallArgs* args) {
    unsigned int fd = args->args[0].as_u64;
    PluginPtr dirpPtr = args->args[1].as_ptr; // struct linux_dirent64*
    unsigned int count = args->args[2].as_u64;

    /* Get and validate the file descriptor. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateFileHelper(sys, fd, &file_desc);
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Path should be non-NULL. */
    if (!dirpPtr.val) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = -EFAULT};
    }

    /* Get the path string from the plugin. */
    struct linux_dirent64* dirp =
        process_getWriteablePtr(sys->process, sys->thread, dirpPtr, sizeof(*dirp));
    if (errcode < 0) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    return (SysCallReturn){
        .state = SYSCALL_DONE,
        .retval.as_i64 = file_getdents64(file_desc, dirp, count)};
}
