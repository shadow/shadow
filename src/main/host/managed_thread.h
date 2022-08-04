#ifndef SRC_MAIN_HOST_SHD_MANAGED_THREAD_H
#define SRC_MAIN_HOST_SHD_MANAGED_THREAD_H

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

typedef struct _ManagedThread ManagedThread;

ManagedThread* managedthread_new(Thread* thread);
void managedthread_free(ManagedThread* mthread);
pid_t managedthread_run(ManagedThread* methread, char* pluginPath, char** argv, char** envv,
                        const char* workingDir);
SysCallCondition* managedthread_resume(ManagedThread* mthread);
void managedthread_handleProcessExit(ManagedThread* mthread);
int managedthread_getReturnCode(ManagedThread* mthread);
bool managedthread_isRunning(ManagedThread* mthread);
ShMemBlock* managedthread_getIPCBlock(ManagedThread* mthread);
long managedthread_nativeSyscall(ManagedThread* mthread, long n, va_list args);
int managedthread_clone(ManagedThread* mthread, unsigned long flags, PluginPtr child_stack,
                        PluginPtr ptid, PluginPtr ctid, unsigned long newtls, Thread** childp);

#endif // SRC_MAIN_HOST_SHD_MANAGED_THREAD_H
