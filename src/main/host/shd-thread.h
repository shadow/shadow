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

// Clone the data at plugin_src into shadow's address space. Prefer this
// over thread_memcpyToShadow for larger or variable size buffers. As a rule of
// thumb, prefer this method over heap-allocating a buffer and then calling
// thread_memcpyToShadow.
//
// The caller has sole ownership of the returned pointer. It must be released
// using thread_releaseClonedPtr.
void* thread_clonePluginPtr(Thread* thread, PluginPtr plugin_src, size_t n);

// Release a pointer returned by thread_clonePluginPtr.
void thread_releaseClonedPtr(Thread* thread, void* p);

// Make the data at plugin_src available in shadow's address space. Prefer this
// over thread_clonePluginPtr or thread_memcpyToShadow if the pointer will no
// longer be needed after returning control to the plugin.
//
// The returned pointer is read-only, and is automatically invalidated when the
// plugin runs again.
const void* thread_readPluginPtr(Thread* thread, PluginPtr plugin_src,
                                 size_t n);

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* thread_writePluginPtr(Thread* thread, PluginPtr plugin_src, size_t n);

gboolean thread_isRunning(Thread* thread);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
