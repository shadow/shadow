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

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/bindings/c/bindings-opaque.h"
#include "main/host/process.h"
#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"

// ***********************************
// For internal use from thread.rs
// ***********************************

typedef struct _Thread Thread;

Thread* cthread_new(const Host* host, const ProcessRefCell* process, const ThreadRc* rustThread, int threadID);
void cthread_free(Thread* thread);

void cthread_run(Thread* thread, const char* pluginPath, const char* const* argv,
                const char* const* envv, const char* workingDir, int straceFd);
void cthread_resume(Thread* thread);
void cthread_handleProcessExit(Thread* thread);
int cthread_getReturnCode(Thread* thread);
long cthread_nativeSyscall(Thread* thread, long n, ...);
bool cthread_isRunning(Thread* thread);
uint32_t cthread_getProcessId(Thread* thread);
HostId cthread_getHostId(Thread* thread);
pid_t cthread_getNativePid(Thread* thread);
pid_t cthread_getNativeTid(Thread* thread);
int cthread_getID(Thread* thread);
int cthread_clone(Thread* thread, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                 PluginPtr ctid, unsigned long newtls, Thread** child);
void cthread_setTidAddress(Thread* thread, PluginVirtualPtr addr);
PluginVirtualPtr cthread_getTidAddress(Thread* thread);
bool cthread_isLeader(Thread* thread);
ShMemBlock* cthread_getIPCBlock(Thread* thread);
ShMemBlock* cthread_getShMBlock(Thread* thread);
ShimShmemThread* cthread_sharedMem(Thread* thread);
const ProcessRefCell* cthread_getProcess(Thread* thread);
const Host* cthread_getHost(Thread* thread);
SysCallHandler* cthread_getSysCallHandler(Thread* thread);
SysCallCondition* cthread_getSysCallCondition(Thread* thread);
void cthread_clearSysCallCondition(Thread* thread);
sigset_t* cthread_getSignalSet(Thread* thread);
bool cthread_unblockedSignalPending(Thread* thread, const ShimShmemHostLock* host_lock);

#endif /* SRC_MAIN_HOST_SHD_THREAD_H_ */
