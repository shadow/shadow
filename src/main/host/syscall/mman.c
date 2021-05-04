/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/syscall/mman.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>

#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/file.h"
#include "main/host/process.h"
#include "main/host/syscall/protected.h"
#include "main/host/thread.h"
#include "main/utility/syscall.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

///////////////////////////////////////////////////////////
// Helpers
///////////////////////////////////////////////////////////

static int _syscallhandler_validateMmapArgsHelper(SysCallHandler* sys, int fd,
                                                  size_t len, int prot,
                                                  int flags,
                                                  File** file_desc_out) {
    /* At least one of these values is required according to man page. */
#ifndef MAP_SHARED_VALIDATE
#define MAP_SHARED_VALIDATE 0x03
#endif
    int reqFlags = (MAP_PRIVATE | MAP_SHARED | MAP_SHARED_VALIDATE);

    /* Need non-zero len, and at least one of the above options. */
    if (len == 0 || !(flags & reqFlags)) {
        info("Invalid len (%zu), prot (%i), or flags (%i)", len, prot, flags);
        return -EINVAL;
    }

    /* We ignore the fd on anonymous mappings, otherwise it must refer to a
     * regular file. */
    if (fd <= 2 && !(flags & MAP_ANONYMOUS)) {
        info("Invalid fd %i and MAP_ANONYMOUS is not set in flags %i", fd,
             flags);
        return -EBADF;
    }

    /* We only need a file if it's not an anonymous mapping. */
    if (!(flags & MAP_ANONYMOUS)) {
        LegacyDescriptor* desc = process_getRegisteredLegacyDescriptor(sys->process, fd);
        int errcode = _syscallhandler_validateDescriptor(desc, DT_NONE);
        if (errcode) {
            info("Invalid fd %i", fd);
            return errcode;
        }

        if (descriptor_getType(desc) != DT_FILE) {
            info("Descriptor exists for fd %i, but is not a file type", fd);
            return -EACCES;
        }

        /* Success. We know we have a file type descriptor. */
        if (file_desc_out) {
            *file_desc_out = (File*)desc;
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
        debug("Unable to produce persistent path to an unopened file.");
        return NULL;
    }

    // We do not use the original file path here, because that path could have been re-linked to a
    // different file since this file was opened.
    char* path = NULL;
    int rv = asprintf(&path, "/proc/%d/fd/%d", getpid(), osfile_fd);

    if (rv < 0) {
        error("asprintf could not allocate a buffer to hold a /proc file path, error %i: %s", errno,
              strerror(errno));
        return NULL;
    }

    // Make sure the path is accessible
    if (path && access(path, F_OK) == 0) {
        debug("File %d (linux file %d) is persistent in procfs at %s", file_fd, osfile_fd, path);
        return path;
    }

    warning(
        "Unable to produce a persistent mmap path for file %d (linux file %d)", file_fd, osfile_fd);
    return NULL;
}

static int _syscallhandler_openPluginFile(SysCallHandler* sys, File* file) {
    utility_assert(file);

    int fd = descriptor_getHandle((LegacyDescriptor*)file);

    debug("Trying to open file %i in the plugin", fd);

    /* TODO: make sure we don't open special files like /dev/urandom,
     * /etc/localtime etc. in the plugin via mmap */

    // The file is in the shadow process, and we want to open it in the plugin.
    char* mmap_path = _file_createPersistentMMapPath(fd, file_getOSBackedFD(file));
    if (mmap_path == NULL) {
        debug("File %i has a NULL path.", fd);
        return -1;
    }

    /* We need enough mem for the string, but no more than PATH_MAX. */
    size_t maplen = strnlen(mmap_path, PATH_MAX - 1) + 1; // an extra 1 for null
    utility_assert(maplen > 1);

    debug("Opening path '%s' in plugin.", mmap_path);

    /* Get some memory in the plugin to write the path of the file to open. */
    AllocdMem_u8 *allocdMem = allocdmem_new(maplen);
    PluginPtr pluginBufPtr = allocdmem_pluginPtr(allocdMem);

    /* Get a writeable pointer that can be flushed to the plugin. */
    char* pluginBuf = process_getWriteablePtr(sys->process, pluginBufPtr, maplen);

    /* Copy the path. */
    snprintf(pluginBuf, maplen, "%s", mmap_path);

    /* Flush the buffer to the plugin. */
    process_flushPtrs(sys->process);

    /* Get original flags that were used to open the file,
       but be careful not to try re-creating or truncating it. */
    int flags = file_getFlags(file) & ~(O_CREAT|O_EXCL|O_TMPFILE|O_TRUNC);

    /* Instruct the plugin to open the file at the path we sent. */
    int result = thread_nativeSyscall(sys->thread, SYS_open, pluginBufPtr.val,
                                      flags, file_getMode(file));
    int err = syscall_rawReturnValueToErrno(result);
    if (err) {
        debug("Failed to open path '%s' in plugin, error %i: %s.", mmap_path, err, strerror(err));
    } else {
        debug("Successfully opened path '%s' in plugin, got plugin fd %i.", mmap_path, result);
    }

    /* Release the PluginPtr memory. */
    allocdmem_free(allocdMem);
    free(mmap_path);

    return result;
}

static void _syscallhandler_closePluginFile(SysCallHandler* sys, int pluginFD) {
    /* Instruct the plugin to close the file at given fd. */
    int result = thread_nativeSyscall(sys->thread, SYS_close, pluginFD);
    int err = syscall_rawReturnValueToErrno(result);
    if (err) {
        debug("Failed to close file at fd %i in plugin, error %i: %s.",
              pluginFD, -result, strerror(-result));
    } else {
        debug("Successfully closed file at fd %i in plugin.", pluginFD);
    }
}

static SysCallReturn _syscallhandler_mmap(SysCallHandler* sys, PluginPtr addrPtr, size_t len,
                                          int prot, int flags, int fd, int64_t offset) {
    debug("mmap called on fd %d for %zu bytes", fd, len);

    /* First check the input args to see if we can avoid doing the less
     * efficient shadow-plugin cross-process mmap procedure. */
    File* file_desc = NULL;
    int errcode = _syscallhandler_validateMmapArgsHelper(
        sys, fd, len, prot, flags, &file_desc);
    if (errcode) {
        return (SysCallReturn){.state = SYSCALL_DONE, .retval.as_i64 = errcode};
    }

    /* Now file_desc is null for an anonymous mapping, non-null otherwise. */
    int pluginFD = -1;

    if (file_desc) {
        pluginFD = _syscallhandler_openPluginFile(sys, file_desc);
        if (pluginFD < 0) {
            warning("mmap on fd %d for %zu bytes failed.", fd, len);
            return (SysCallReturn){
                .state = SYSCALL_DONE, .retval.as_i64 = -EACCES};
        }
    }

    // Delegate execution of the mmap itself to the memorymanager.
    MemoryManager* mm = process_getMemoryManager(sys->process);
    SysCallReturn result =
        memorymanager_handleMmap(mm, sys->thread, addrPtr, len, prot, flags, pluginFD, offset);
    if (result.state == SYSCALL_NATIVE) {
        result = (SysCallReturn){.state = SYSCALL_DONE,
                                 .retval = thread_nativeSyscall(sys->thread, SYS_mmap, addrPtr, len,
                                                                prot, flags, pluginFD, offset)};
    }

    debug("Plugin-native mmap syscall at plugin addr %p with plugin fd %i for "
          "%zu bytes returned %p (%s)",
          (void*)addrPtr.val, pluginFD, len, (void*)result.retval.as_u64,
          strerror(syscall_rawReturnValueToErrno(result.retval.as_i64)));

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

SysCallReturn syscallhandler_brk(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr newBrk = args->args[0].as_ptr;

    // Delegate to the memoryManager.
    MemoryManager* mm = process_getMemoryManager(sys->process);
    return memorymanager_handleBrk(mm, sys->thread, newBrk);
}

SysCallReturn syscallhandler_mmap(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr addrPtr = args->args[0].as_ptr; // void*
    size_t len = args->args[1].as_u64;
    int prot = args->args[2].as_i64;
    int flags = args->args[3].as_i64;
    int fd = args->args[4].as_i64;
    int64_t offset = args->args[5].as_i64;
    return _syscallhandler_mmap(sys, addrPtr, len, prot, flags, fd, offset);
}

SysCallReturn syscallhandler_mmap2(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr addrPtr = args->args[0].as_ptr; // void*
    size_t len = args->args[1].as_u64;
    int prot = args->args[2].as_i64;
    int flags = args->args[3].as_i64;
    int fd = args->args[4].as_i64;
    int64_t pgoffset = args->args[5].as_i64;

    // As long as we're on a system where off_t is 64-bit, we can just remap to mmap.
    utility_assert(sizeof(off_t) == sizeof(int64_t));
    return _syscallhandler_mmap(sys, addrPtr, len, prot, flags, fd, 4096 * pgoffset);
}

SysCallReturn syscallhandler_mremap(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr old_addr = args->args[0].as_ptr;
    uint64_t old_size = args->args[1].as_u64;
    uint64_t new_size = args->args[2].as_u64;
    int flags = args->args[3].as_i64;
    PluginPtr new_addr = args->args[4].as_ptr;

    // Delegate to the memoryManager.
    MemoryManager* mm = process_getMemoryManager(sys->process);
    return memorymanager_handleMremap(
        mm, sys->thread, old_addr, old_size, new_size, flags, new_addr);
}

SysCallReturn syscallhandler_munmap(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr addr = args->args[0].as_ptr;
    uint64_t len = args->args[1].as_u64;

    // Delegate to the memoryManager.
    MemoryManager* mm = process_getMemoryManager(sys->process);
    return memorymanager_handleMunmap(mm, sys->thread, addr, len);
}

SysCallReturn syscallhandler_mprotect(SysCallHandler* sys, const SysCallArgs* args) {
    PluginPtr addr = args->args[0].as_ptr;
    size_t len = args->args[1].as_u64;
    int prot = args->args[2].as_i64;

    // Delegate to the memoryManager.
    MemoryManager* mm = process_getMemoryManager(sys->process);
    return memorymanager_handleMprotect(mm, sys->thread, addr, len, prot);
}
