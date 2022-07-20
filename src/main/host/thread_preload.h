#ifndef SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_
#define SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

typedef struct _ManagedThread ManagedThread;

Thread* managedthread_new(Host* host, Process* process, gint threadID);
void managedthread_free(Thread* base);
pid_t managedthread_run(Thread* base, char* pluginPath, char** argv, char** envv,
                        const char* workingDir);
SysCallCondition* managedthread_resume(Thread* base);
void managedthread_handleProcessExit(Thread* base);
int managedthread_getReturnCode(Thread* base);
bool managedthread_isRunning(Thread* base);
ShMemBlock* managedthread_getIPCBlock(Thread* base);
long managedthread_nativeSyscall(Thread* base, long n, va_list args);
int managedthread_clone(Thread* base, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                        PluginPtr ctid, unsigned long newtls, Thread** childp);

#endif // SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_
