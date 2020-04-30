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

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"

Thread* thread_new(gint threadID, SysCallHandler* sys);
void thread_ref(Thread* thread);
void thread_unref(Thread* thread);

void thread_run(Thread* thread, gchar** argv, gchar** envv);
void thread_resume(Thread* thread);
void thread_terminate(Thread* thread);
int thread_getReturnCode(Thread* thread);

// Make the data at plugin_src available in shadow's address space.
//
// The returned pointer is read-only, and is automatically invalidated when the
// plugin runs again.
const void* thread_getReadablePtr(Thread* thread, PluginPtr plugin_src,
                                  size_t n);

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* thread_getWriteablePtr(Thread* thread, PluginPtr plugin_src, size_t n);

// Clone the data at plugin_src into shadow's address space.
//
// The caller has sole ownership of the returned pointer. It must be released
// using thread_releaseClonedPtr.
void* thread_newClonedPtr(Thread* thread, PluginPtr plugin_src, size_t n);

// Release a pointer returned by thread_clonePluginPtr.
void thread_releaseClonedPtr(Thread* thread, void* p);

gboolean thread_isRunning(Thread* thread);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
