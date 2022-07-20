#ifndef SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_
#define SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

typedef struct _ThreadPreload ThreadPreload;

Thread* threadpreload_new(Host* host, Process* process, gint threadID);
void threadpreload_free(Thread* base);
pid_t threadpreload_run(Thread* base, char* pluginPath, char** argv, char** envv,
                        const char* workingDir);
SysCallCondition* threadpreload_resume(Thread* base);
void threadpreload_handleProcessExit(Thread* base);
int threadpreload_getReturnCode(Thread* base);
bool threadpreload_isRunning(Thread* base);
ShMemBlock* threadpreload_getIPCBlock(Thread* base);
long threadpreload_nativeSyscall(Thread* base, long n, va_list args);
int threadpreload_clone(Thread* base, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                        PluginPtr ctid, unsigned long newtls, Thread** childp);

#endif // SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_
