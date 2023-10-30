/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/mman.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/utility/syscall.h"
#include "main/utility/utility.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateMmapArgsHelper(SysCallHandler* sys, int fd, size_t len, int prot,
                                                  int flags, RegularFile** file_desc_out) {
    /* At least one of these values is required according to man page. */
#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE 0x03
#endif
    int reqFlags = (MAP_PRIVATE | MAP_SHARED | MAP_SHARED_VALIDATE);

    /* Need non-zero len, and at least one of the above options. */
    if (len == 0 || !(flags & reqFlags)) {
        debug("Invalid len (%zu), prot (%i), or flags (%i)", len, prot, flags);
        return -EINVAL;
    }

    /* We ignore the fd on anonymous mappings, otherwise it must refer to a
     * regular file. */
    if (fd <= 2 && !(flags & MAP_ANONYMOUS)) {
        debug("Invalid fd %i and MAP_ANONYMOUS is not set in flags %i", fd, flags);
        return -EBADF;
    }

    /* We only need a file if it's not an anonymous mapping. */
    if (!(flags & MAP_ANONYMOUS)) {
        LegacyFile* desc = thread_getRegisteredLegacyFile(_syscallhandler_getThread(sys), fd);
        int errcode = _syscallhandler_validateLegacyFile(desc, DT_NONE);
        if (errcode) {
            debug("Invalid fd %i", fd);
            return errcode;
        }

        if (legacyfile_getType(desc) != DT_FILE) {
            debug("Descriptor exists for fd %i, but is not a file type", fd);
            return -EACCES;
        }

        /* Success. We know we have a file type descriptor. */
        if (file_desc_out) {
            *file_desc_out = (RegularFile*)desc;
        }
    }

    return 0;
}

/* Get a path to a persistent file that can be mmapped in a child process,
 * where any I/O operations on the map will be linked to the original file.
 * Returns a new string holding the path, or NULL if we are unable to create an accessible path.
 * The caller should free the path string when appropriate.  */
static char* _file_createPersistentMMapPath(int file_fd, int osfile_fd) {
    // Return a path that is linked to the I/O operations of the file. Our current strategy is to
    // have the plugin open and map the /proc/<shadow-pid>/fd/<linux-fd> file, which guarantees that
    // the I/O on the Shadow file object and the new map will be linked to the linux file.
    // TODO: using procfs in this was may or may not work if trying to mmap a device.
    //
    // NOTE: If we need to change this implementation, there are two tricky cases that need to be
    // considered: files opened with O_TMPFILE (with a directory pathname), and files that were
    // opened and then immediately unlinked (so only the anonymous fd remains). The procfs solution
    // above handles both of these issues.

    // Handle the case where the OS file has not been opened yet.
    if (osfile_fd < 0) {
        trace("Unable to produce persistent path to an unopened file.");
        return NULL;
    }

    // We do not use the original file path here, because that path could have been re-linked to a
    // different file since this file was opened.
    char* path = NULL;
    int rv = asprintf(&path, "/proc/%d/fd/%d", getpid(), osfile_fd);

    if (rv < 0) {
        utility_panic(
            "asprintf could not allocate a buffer to hold a /proc file path, error %i: %s", errno,
            strerror(errno));
        return NULL;
    }

    // Make sure the path is accessible
    if (path && access(path, F_OK) == 0) {
        trace("RegularFile %d (linux file %d) is persistent in procfs at %s", file_fd, osfile_fd,
              path);
        return path;
    }

    warning(
        "Unable to produce a persistent mmap path for file %d (linux file %d)", file_fd, osfile_fd);
    return NULL;
}

static int _syscallhandler_openPluginFile(SysCallHandler* sys, int fd, RegularFile* file) {
    utility_debugAssert(file);
    int result = 0;

    trace("Trying to open file %i in the plugin", fd);

    /* TODO: make sure we don't open special files like /dev/urandom,
     * /etc/localtime etc. in the plugin via mmap */

    // The file is in the shadow process, and we want to open it in the plugin.
    char* mmap_path = _file_createPersistentMMapPath(fd, regularfile_getOSBackedFD(file));
    if (mmap_path == NULL) {
        trace("RegularFile %i has a NULL path.", fd);
        return -1;
    }

    /* We need enough mem for the string, but no more than PATH_MAX. */
    size_t maplen = strnlen(mmap_path, PATH_MAX - 1) + 1; // an extra 1 for null
    utility_debugAssert(maplen > 1);

    trace("Opening path '%s' in plugin.", mmap_path);

    /* Get some memory in the plugin to write the path of the file to open. */
    AllocdMem_u8* allocdMem = allocdmem_new(_syscallhandler_getThread(sys), maplen);
    UntypedForeignPtr foreignBufPtr = allocdmem_foreignPtr(allocdMem);

    /* Get a writeable pointer that can be flushed to the plugin. */
    char* pluginBuf =
        process_getWriteablePtr(_syscallhandler_getProcess(sys), foreignBufPtr, maplen);

    /* Copy the path. */
    snprintf(pluginBuf, maplen, "%s", mmap_path);

    /* Flush the buffer to the plugin. */
    result = process_flushPtrs(_syscallhandler_getProcess(sys));
    if (result) {
        goto out;
    }

    /* Attempt to open the file in the plugin with the same flags as what the
     * shadow RegularFile object has. */

    /* From man 2 open */
    const int creationFlags =
        O_CLOEXEC | O_CREAT | O_DIRECTORY | O_EXCL | O_NOCTTY | O_NOFOLLOW | O_TMPFILE | O_TRUNC;

    /* Get original flags that were used to open the file. */
    int flags = regularfile_getFlagsAtOpen(file);
    /* Use only the file creation flags, except O_CLOEXEC. */
    flags &= (creationFlags & ~O_CLOEXEC);
    /* Add any file access mode and file status flags that shadow doesn't implement. */
    flags |= (fcntl(regularfile_getOSBackedFD(file), F_GETFL) & ~SHADOW_FLAG_MASK);
    /* Add any flags that shadow implements. */
    flags |= regularfile_getShadowFlags(file);
    /* Be careful not to try re-creating or truncating it. */
    flags &= ~(O_CREAT | O_EXCL | O_TMPFILE | O_TRUNC);
    /* Don't use O_NOFOLLOW since it will prevent the plugin from
     * opening the /proc/<shadow-pid>/fd/<linux-fd> file, which is a symbolic link. */
    flags &= ~O_NOFOLLOW;

    /* Instruct the plugin to open the file at the path we sent. */
    result = thread_nativeSyscall(_syscallhandler_getThread(sys), SYS_open, foreignBufPtr.val,
                                  flags, regularfile_getModeAtOpen(file));
    int err = syscall_rawReturnValueToErrno(result);
    if (err) {
        trace("Failed to open path '%s' in plugin, error %i: %s.", mmap_path, err, strerror(err));
    } else {
        trace("Successfully opened path '%s' in plugin, got plugin fd %i.", mmap_path, result);
    }

out:
    /* Release the UntypedForeignPtr memory. */
    allocdmem_free(_syscallhandler_getThread(sys), allocdMem);
    free(mmap_path);

    return result;
}

static void _syscallhandler_closePluginFile(SysCallHandler* sys, int pluginFD) {
    /* Instruct the plugin to close the file at given fd. */
    int result = thread_nativeSyscall(_syscallhandler_getThread(sys), SYS_close, pluginFD);
    int err = syscall_rawReturnValueToErrno(result);
    if (err) {
        trace("Failed to close file at fd %i in plugin, error %i: %s.",
              pluginFD, -result, strerror(-result));
    } else {
        trace("Successfully closed file at fd %i in plugin.", pluginFD);
    }
}

static SyscallReturn _syscallhandler_mmap(SysCallHandler* sys, UntypedForeignPtr addrPtr,
                                          size_t len, int prot, int flags, int fd, int64_t offset) {
    trace("mmap called on fd %d for %zu bytes", fd, len);

    /* First check the input args to see if we can avoid doing the less
     * efficient shadow-plugin cross-process mmap procedure. */
    RegularFile* file_desc = NULL;
    int errcode = _syscallhandler_validateMmapArgsHelper(
        sys, fd, len, prot, flags, &file_desc);
    if (errcode) {
        return syscallreturn_makeDoneErrno(-errcode);
    }

    /* Now file_desc is null for an anonymous mapping, non-null otherwise. */
    int pluginFD = -1;

    if (file_desc) {
        pluginFD = _syscallhandler_openPluginFile(sys, fd, file_desc);
        if (pluginFD < 0) {
            warning("mmap on fd %d for %zu bytes failed.", fd, len);
            return syscallreturn_makeDoneErrno(EACCES);
        }
    }

    // Delegate execution of the mmap itself to the memorymanager.
    SyscallReturn result =
        process_handleMmap(_syscallhandler_getProcess(sys), _syscallhandler_getThread(sys), addrPtr,
                           len, prot, flags, pluginFD, offset);
    if (result.tag == SYSCALL_RETURN_NATIVE) {
        return syscallreturn_makeDoneI64(thread_nativeSyscall(
            _syscallhandler_getThread(sys), SYS_mmap, addrPtr, len, prot, flags, pluginFD, offset));
    }

    trace("Plugin-native mmap syscall at plugin addr %p with plugin fd %i for "
          "%zu bytes returned %p",
          (void*)addrPtr.val, pluginFD, len, (void*)syscallreturn_done(&result)->retval.as_u64);

    /* Close the file we asked them to open. */
    if (pluginFD >= 0) {
        _syscallhandler_closePluginFile(sys, pluginFD);
    }

    /* Done! Return their result back to them. */
    return result;
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SyscallReturn syscallhandler_mmap(SysCallHandler* sys, const SysCallArgs* args) {
    UntypedForeignPtr addrPtr = args->args[0].as_ptr; // void*
    size_t len = args->args[1].as_u64;
    int prot = args->args[2].as_i64;
    int flags = args->args[3].as_i64;
    int fd = args->args[4].as_i64;
    int64_t offset = args->args[5].as_i64;
    return _syscallhandler_mmap(sys, addrPtr, len, prot, flags, fd, offset);
}
