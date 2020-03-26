#ifndef SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_
#define SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_

/*
 * Implementation details for the Thread interface.
 *
 * This file should only be included by C files *implementing* the Thread
 * interface.
 */

#include "shim/shim-event.h"
#include "main/host/shd-thread.h"

struct _Thread {
    void (*run)(Thread* thread, gchar** argv, gchar** envv);
    void (*resume)(Thread* thread);
    void (*terminate)(Thread* thread);
    void (*setSysCallResult)(Thread* thread, SysCallReg retval);
    int (*getReturnCode)(Thread* thread);
    gboolean (*isRunning)(Thread* thread);
    void (*free)(Thread* thread);
    void* (*clonePluginPtr)(Thread* base, PluginPtr plugin_src, size_t n);
    void (*releaseClonedPtr)(Thread* base, void* p);
    const void* (*readPluginPtr)(Thread* thread, PluginPtr plugin_src,
                                 size_t n);
    void* (*writePluginPtr)(Thread* thread, PluginPtr plugin_src, size_t n);

    // For safe down-casting. Set and checked by child class.
    int type_id;

    int referenceCount;

    MAGIC_DECLARE;
};

#endif
