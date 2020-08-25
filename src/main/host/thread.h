/*
 * shd-thread.h
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_THREAD_H_
#define SRC_MAIN_HOST_SHD_THREAD_H_

#include <stddef.h>
#include <sys/types.h>

typedef struct _Thread Thread;

#include "main/host/syscall_types.h"

void thread_ref(Thread* thread);
void thread_unref(Thread* thread);

void thread_run(Thread* thread, char** argv, char** envv);
void thread_resume(Thread* thread);
void thread_terminate(Thread* thread);
int thread_getReturnCode(Thread* thread);

// Make the data at plugin_src available in shadow's address space.
//
// The returned pointer is read-only, and is automatically invalidated when the
// plugin runs again.
const void* thread_getReadablePtr(Thread* thread, PluginPtr plugin_src,
                                  size_t n);

// Make the data starting at plugin_src, and extending until the first NULL
// byte, up at most `n` bytes, available in shadow's address space.
//
// * `str` must be non-NULL, and is set to point to the given string. It is
//   invalidated when the plugin runs again.
// * `strlen` may be NULL. If it isn't, is set to `strlen(str)`.
//
// Returns:
// 0 on success.
// -ENAMETOOLONG if there was no NULL byte in the first `n` characters.
// -EFAULT if the string extends beyond the accessible address space.
int thread_getReadableString(Thread* thread, PluginPtr plugin_src, size_t n,
                             const char** str, size_t* strlen);

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* thread_getWriteablePtr(Thread* thread, PluginPtr plugin_src, size_t n);

// Returns a writeable pointer corresponding to the specified src. Use when
// the data at the given address needs to be both read and written.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* thread_getMutablePtr(Thread* thread, PluginPtr plugin_src, size_t n);

// Flushes and invalidates all previously returned readable/writeable plugin
// pointers, as if returning control to the plugin. This can be useful in
// conjunction with `thread_nativeSyscall` operations that touch memory.
void thread_flushPtrs(Thread* thread);

// Make the requested syscall from within the plugin. For now, does *not* flush
// or invalidate pointers, but we may need to revisit this to support some
// use-cases.
//
// Arguments are treated opaquely. e.g. no pointer-marshalling is done.
//
// The return value is the value returned by the syscall *instruction*.
// You can map to a corresponding errno value with syscall_rawReturnValueToErrno.
long thread_nativeSyscall(Thread* thread, long n, ...);

// Allocate some memory in the plugin's address space. The returned pointer
// should be freed with `thread_free`.
PluginPtr thread_mallocPluginPtr(Thread* thread, size_t size);

// Free memory allocated with `thread_mallocPluginPtr`. `size` should be the
// original size passed to `thread_mallocPluginPtr`.
//
// TODO: It's a bit unfortunate to have to require the size here, but at this
// time the underlying implementation (based on mmap) needs it. The alternatives
// to this API awkwardness is either for thread_mallocPluginPtr to return an
// opaque struct where this can be squirreled away (a different kind of API
// awkwardness and more boilerplate), or keeping an internal map of ptr->size.
void thread_freePluginPtr(Thread* thread, PluginPtr ptr, size_t size);

bool thread_isRunning(Thread* thread);

uint32_t thread_getProcessId(Thread* thread);

uint32_t thread_getHostId(Thread* thread);

pid_t thread_getNativePid(Thread* thread);
pid_t thread_getNativeTid(Thread* thread);

int thread_getID(Thread* thread);
int thread_clone(Thread* thread, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                     PluginPtr ctid, unsigned long newtls, Thread** child);

void thread_setTidAddress(Thread* thread, PluginPtr addr);

bool thread_isLeader(Thread* thread);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
