#ifndef SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_
#define SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_

/*
 * Implementation details for the Thread interface.
 *
 * This file should only be included by C files *implementing* the Thread
 * interface.
 */

#include "main/host/thread.h"
#include "shim/shim_event.h"

struct _Thread {
    void (*run)(Thread* thread, gchar** argv, gchar** envv);
    void (*resume)(Thread* thread);
    void (*terminate)(Thread* thread);
    int (*getReturnCode)(Thread* thread);
    gboolean (*isRunning)(Thread* thread);
    void (*free)(Thread* thread);
    void* (*newClonedPtr)(Thread* base, PluginPtr plugin_src, size_t n);
    void (*releaseClonedPtr)(Thread* base, void* p);
    const void* (*getReadablePtr)(Thread* thread, PluginPtr plugin_src,
                                  size_t n);
    int (*getReadableString)(Thread* thread, PluginPtr plugin_src, size_t n,
                             const char** str, size_t* strlen);
    void* (*getWriteablePtr)(Thread* thread, PluginPtr plugin_src, size_t n);
    void (*flushPtrs)(Thread* thread);
    long (*nativeSyscall)(Thread* thread, long n, va_list args);

    // For safe down-casting. Set and checked by child class.
    int type_id;

    int referenceCount;

    MAGIC_DECLARE;
};

#endif
