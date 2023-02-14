#ifndef SRC_MAIN_HOST_SHD_MANAGED_THREAD_H
#define SRC_MAIN_HOST_SHD_MANAGED_THREAD_H

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

typedef struct _ManagedThread ManagedThread;

ManagedThread* managedthread_new(HostId hostId, pid_t processId, pid_t threadId);
void managedthread_free(ManagedThread* mthread);
void managedthread_run(ManagedThread* methread, const char* pluginPath, const char* const* argv,
                       const char* const* envv, const char* workingDir, int straceFd,
                       const char* logPath);
SysCallCondition* managedthread_resume(ManagedThread* mthread);
void managedthread_handleProcessExit(ManagedThread* mthread);
int managedthread_getReturnCode(ManagedThread* mthread);
bool managedthread_isRunning(ManagedThread* mthread);
ShMemBlock* managedthread_getIPCBlock(ManagedThread* mthread);
long managedthread_nativeSyscall(ManagedThread* mthread, long n, va_list args);
// Execute a clone syscall in `parent`, and initialize `child` to manage the new
// native thread.  Returns 0 on success or a negative errno on failure.
int managedthread_clone(ManagedThread* child, ManagedThread* parent, unsigned long flags,
                        PluginPtr child_stack, PluginPtr ptid, PluginPtr ctid,
                        unsigned long newtls);
// XXX Can we avoid exposing these?
pid_t managedthread_getNativePid(ManagedThread* mthread);
pid_t managedthread_getNativeTid(ManagedThread* mthread);

#endif // SRC_MAIN_HOST_SHD_MANAGED_THREAD_H
