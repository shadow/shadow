/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/thread.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "main/host/syscall_handler.h"
#include "main/host/thread_protected.h"
#include "main/utility/syscall.h"
#include "shim/shim_event.h"
#include "support/logger/logger.h"

static size_t _pageSize() {
    return (size_t)sysconf(_SC_PAGESIZE);
}


static PluginPtr _pageOf(PluginPtr p) {
    return (PluginPtr){p.val & ~(_pageSize()-1)};
}

// FIXME: copy-pasta
static int _openFile(Thread* thread, const char* file) {
    utility_assert(file);

    /* We need enough mem for the string, but no more than PATH_MAX. */
    size_t maplen = strlen(file) + 1; // an extra 1 for null

    debug("Opening shm path '%s' in plugin.", file);

    /* Get some memory in the plugin to write the path of the file to open. */
    PluginPtr pluginBufPtr = thread_mallocPluginPtr(thread, maplen);

    /* Get a writeable pointer that can be flushed to the plugin. */
    char* pluginBuf = thread->getWriteablePtr(thread, pluginBufPtr, maplen);

    /* Copy the path. */
    strcpy(pluginBuf, file);

    /* Flush the buffer to the plugin. */
    thread->flushPtrs(thread);

    /* Instruct the plugin to open the file at the path we sent. */
    int result = thread_nativeSyscall(thread, SYS_open, pluginBufPtr.val, O_RDWR);
    int err = syscall_rawReturnValueToErrno(result);
    if (err) {
        debug("Failed to open path '%s' in plugin, error %i: %s.", file, err, strerror(err));
    } else {
        debug("Successfully opened path '%s' in plugin, got plugin fd %i.",
              file, result);
    }

    /* Release the PluginPtr memory. */
    thread_freePluginPtr(thread, pluginBufPtr, maplen);

    return result;
}

static void _closeFile(Thread* thread, int pluginFD) {
    /* Instruct the plugin to close the file at given fd. */
    int result = thread_nativeSyscall(thread, SYS_close, pluginFD);
    int err = syscall_rawReturnValueToErrno(result);
    if (err) {
        debug("Failed to close file at fd %i in plugin, error %i: %s.",
              pluginFD, -result, strerror(-result));
    } else {
        debug("Successfully closed file at fd %i in plugin.", pluginFD);
    }
}


void thread_init(Thread* thread) {
    thread->pluginPtrToPtr = g_hash_table_new(g_direct_hash, g_direct_equal);
}

static void* mappage(Thread* thread, PluginPtr aligned_plugin_ptr) {
    utility_assert(_pageOf(aligned_plugin_ptr).val == aligned_plugin_ptr.val);

    // Create a shmem file
    static int count = 0;
    char *file;
    asprintf(&file, "/dev/shm/shadow_page_%d_%d", (int)getpid(), ++count);
    int fd = open(file, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (fd < 0) {
        error("shm_open: %s", strerror(errno));
        abort();
    }

    // Set the size
    debug("ftruncate %d:%s to %zu", fd, file, _pageSize());
    if (ftruncate(fd, _pageSize()) < 0) {
        error("ftruncate: %s", strerror(errno));
        abort();
    }
 
    // Map the shmem file
    void *mapped_ptr = mmap(NULL, _pageSize(), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_ptr == MAP_FAILED) {
        error("mmap: %s", strerror(errno));
        abort();
    }

    // Copy data into the shmem file
    const void *ptr = thread->getReadablePtr(thread, aligned_plugin_ptr, _pageSize());
    memcpy(mapped_ptr, ptr, _pageSize());

    // Have the plugin map the shmem file
    int plugin_fd = _openFile(thread, file);
    if (plugin_fd < 0) {
        abort();
    }
    long res = thread_nativeSyscall(thread, SYS_mmap, aligned_plugin_ptr.val, _pageSize(),
                                    PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, plugin_fd, 0);
    int err = syscall_rawReturnValueToErrno(res);
    if (err) {
        error("thread_nativeSyscall(mmap): %s", strerror(err));
        abort();
    }

    free(file);

    return mapped_ptr;
}

static void* pluginPtrToPtr(Thread* thread, PluginPtr p) {
    PluginPtr plugin_base = _pageOf(p);
    size_t offset = p.val - plugin_base.val;

    // Get from hash table.
    void* shadow_base = g_hash_table_lookup(thread->pluginPtrToPtr, GUINT_TO_POINTER(plugin_base.val));

    // If missing, add it.
    if (!shadow_base) {
        shadow_base = mappage(thread, plugin_base);
        utility_assert(shadow_base);
        g_hash_table_insert(thread->pluginPtrToPtr, GUINT_TO_POINTER(plugin_base.val), shadow_base);
    }

    return shadow_base + offset;
}

void thread_enableOpt(Thread* thread) {
    thread->optEnabled = true;
}

void thread_ref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)++;
}

void thread_unref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)--;
    utility_assert(thread->referenceCount >= 0);
    if(thread->referenceCount == 0) {
        thread->free(thread);
    }
}

void thread_run(Thread* thread, gchar** argv, gchar** envv) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->run);
    thread->run(thread, argv, envv);
}

SysCallCondition* thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->resume);
    return thread->resume(thread);
}

void thread_terminate(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->terminate);
    thread->terminate(thread);
}
int thread_getReturnCode(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReturnCode);
    return thread->getReturnCode(thread);
}

gboolean thread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->isRunning(thread);
}

void* thread_newClonedPtr(Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->newClonedPtr);
    return thread->newClonedPtr(thread, plugin_src, n);
}

void thread_releaseClonedPtr(Thread* thread, void* p) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->releaseClonedPtr);
    thread->releaseClonedPtr(thread, p);
}

const void* thread_getReadablePtr(Thread* thread, PluginPtr plugin_src,
                                  size_t n) {
    MAGIC_ASSERT(thread);
    if (thread->optEnabled && _pageOf(plugin_src).val == _pageOf((PluginPtr){plugin_src.val + n}).val) {
        //info("readable using mapped pointer for %p %zd", (void*)plugin_src.val, n);
        return pluginPtrToPtr(thread, plugin_src);
    }
    //info("readable Couldn't use mapped pointer for %p %zd", (void*)plugin_src.val, n);
    utility_assert(thread->getReadablePtr);
    return thread->getReadablePtr(thread, plugin_src, n);
}

int thread_getReadableString(Thread* thread, PluginPtr plugin_src, size_t n,
                             const char** str, size_t* strlen) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->getReadableString);
    return thread->getReadableString(thread, plugin_src, n, str, strlen);
}

void* thread_getWriteablePtr(Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(thread);
    if (thread->optEnabled && _pageOf(plugin_src).val == _pageOf((PluginPtr){plugin_src.val + n}).val) {
        //info("writeable using mapped pointer for %p %zd", (void*)plugin_src.val, n);
        return pluginPtrToPtr(thread, plugin_src);
    }
    //info("writeable Couldn't use mapped pointer for %p %zd", (void*)plugin_src.val, n);
    utility_assert(thread->getReadablePtr);
    return thread->getWriteablePtr(thread, plugin_src, n);
}

void thread_flushPtrs(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->flushPtrs);
    thread->flushPtrs(thread);
}

long thread_nativeSyscall(Thread* thread, long n, ...) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->nativeSyscall);
    va_list(args);
    va_start(args, n);
    long rv = thread->nativeSyscall(thread, n, args);
    va_end(args);
    return rv;
}

PluginPtr thread_mallocPluginPtr(Thread* thread, size_t size) {
    // For now we just implement in terms of thread_nativeSyscall.
    // TODO: We might be able to do something more efficient by delegating to
    // the specific thread implementation, and/or keeping a persistent
    // mmap'd area that we allocate from.
    MAGIC_ASSERT(thread);
    long ptr = thread_nativeSyscall(thread, SYS_mmap, NULL, size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int err = syscall_rawReturnValueToErrno(ptr);
    if (err) {
        error("thread_nativeSyscall(mmap): %s", strerror(err));
        abort();
    }

    // Should be page-aligned.
    utility_assert((ptr % _pageSize()) == 0);

    return (PluginPtr){.val = ptr};
}

void thread_freePluginPtr(Thread* thread, PluginPtr ptr, size_t size) {
    MAGIC_ASSERT(thread);
    int err =
        syscall_rawReturnValueToErrno(thread_nativeSyscall(thread, SYS_munmap, ptr.val, size));
    if (err) {
        error("thread_nativeSyscall(munmap): %s", strerror(err));
        abort();
    }
}
