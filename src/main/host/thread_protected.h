#ifndef SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_
#define SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_

/*
 * Implementation details for the Thread interface.
 *
 * This file should only be included by C files *implementing* the Thread
 * interface.
 */

#include <stdarg.h>

#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"
#include "shim/shim_event.h"

typedef struct _ThreadMethods {
    pid_t (*run)(Thread* thread, char** argv, char** envv);
    SysCallCondition* (*resume)(Thread* thread);
    void (*terminate)(Thread* thread);
    int (*getReturnCode)(Thread* thread);
    bool (*isRunning)(Thread* thread);
    void (*free)(Thread* thread);
    void* (*newClonedPtr)(Thread* base, PluginPtr plugin_src, size_t n);
    void (*releaseClonedPtr)(Thread* base, void* p);
    const void* (*getReadablePtr)(Thread* thread, PluginPtr plugin_src, size_t n);
    int (*getReadableString)(Thread* thread, PluginPtr plugin_src, size_t n, const char** str,
                             size_t* strlen);
    void* (*getWriteablePtr)(Thread* thread, PluginPtr plugin_src, size_t n);
    void (*flushPtrs)(Thread* thread);
    long (*nativeSyscall)(Thread* thread, long n, va_list args);
} ThreadMethods;

struct _Thread {
    // For safe down-casting. Set and checked by child class.
    int type_id;

    ThreadMethods methods;
    int threadID;
    pid_t nativePid;
    Host* host;
    Process* process;
    int referenceCount;

    MAGIC_DECLARE;
};

Thread thread_create(Host* host, Process* process, int type_id, ThreadMethods methods);
#endif
