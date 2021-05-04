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
#include "main/shmem/shmem_allocator.h"

void thread_ref(Thread* thread);
void thread_unref(Thread* thread);

void thread_run(Thread* thread, char** argv, char** envv, const char* workingDir);
void thread_resume(Thread* thread);
void thread_handleProcessExit(Thread* thread);
int thread_getReturnCode(Thread* thread);

// Make the requested syscall from within the plugin. For now, does *not* flush
// or invalidate pointers, but we may need to revisit this to support some
// use-cases.
//
// Arguments are treated opaquely. e.g. no pointer-marshalling is done.
//
// The return value is the value returned by the syscall *instruction*.
// You can map to a corresponding errno value with syscall_rawReturnValueToErrno.
long thread_nativeSyscall(Thread* thread, long n, ...);

bool thread_isRunning(Thread* thread);

uint32_t thread_getProcessId(Thread* thread);

uint32_t thread_getHostId(Thread* thread);

pid_t thread_getNativePid(Thread* thread);
pid_t thread_getNativeTid(Thread* thread);

// Returns the Shadow thread id.
int thread_getID(Thread* thread);

// Create a new child thread as for `clone(2)`. Returns the child pid, or a
// negative errno.  If the returned pid is >= 0, then `child` will be set to a
// newly allocated and initialized child Thread. Caller is responsible for
// adding the Thread to the process and arranging for it to run (typically by
// calling process_addThread).
int thread_clone(Thread* thread, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                 PluginPtr ctid, unsigned long newtls, Thread** child);

// Sets the `clear_child_tid` attribute as for `set_tid_address(2)`. The thread
// will perform a futex-wake operation on the given address on termination.
void thread_setTidAddress(Thread* thread, PluginVirtualPtr addr);

// Gets the `clear_child_tid` attribute, as set by `thread_setTidAddress`.
PluginVirtualPtr thread_getTidAddress(Thread* thread);

// Returns whether the given thread is its thread group (aka process) leader.
// Typically this is true for the first thread created in a process.
bool thread_isLeader(Thread* thread);

// Returns the block used for IPC, or NULL if no such block is is used.
ShMemBlock* thread_getIPCBlock(Thread* thread);

// Returns the block used for shared state, or NULL if no such block is is used.
ShMemBlock* thread_getShMBlock(Thread* thread);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
