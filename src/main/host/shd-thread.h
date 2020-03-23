/*
 * shd-thread.h
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_THREAD_H_
#define SRC_MAIN_HOST_SHD_THREAD_H_

#include <glib.h>

typedef struct _Thread Thread;

#include "main/host/shd-syscall-handler.h"
#include "main/host/shd-syscall-types.h"

Thread* thread_new(gint threadID, SysCallHandler* sys);
void thread_ref(Thread* thread);
void thread_unref(Thread* thread);

void thread_run(Thread* thread, gchar** argv, gchar** envv);
void thread_resume(Thread* thread);
void thread_terminate(Thread* thread);
void thread_setSysCallResult(Thread* thread, SysCallReg retval);
int thread_getReturnCode(Thread* thread);

// Copy data from the plugin's address space.
void thread_memcpyToShadow(Thread* thread, void* shadow_dst,
                           PluginPtr plugin_src, size_t n);

// Copy data to the plugin's address space.
void thread_memcpyToPlugin(Thread* thread, PluginPtr plugin_dst,
                           void* shadow_src, size_t n);

// Make the data at plugin_src available in shadow's address space. Prefer this
// over thread_memcpyToShadow for larger or variable size buffers. As a rule of
// thumb, prefer this method over heap-allocating a buffer and then calling
// thread_memcpyToShadow.
//
// The returned pointer must be released using thread_releaseClonedPtr.
void* thread_clonePluginPtr(Thread* thread, PluginPtr plugin_src, size_t n);

// Release a pointer returned by thread_clonePluginPtr.
void thread_releaseClonedPtr(Thread* thread, void* p);

gboolean thread_isRunning(Thread* thread);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
