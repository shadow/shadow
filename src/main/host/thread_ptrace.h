#ifndef SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_
#define SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

// Create a thread managed via ptrace and shim-ipc.
Thread* threadptrace_new(Host* host, Process* process, gint threadID);

// Create a thread managed via ptrace only.
Thread* threadptracenoipc_new(Host* host, Process* process, gint threadID);

void threadptrace_detach(Thread* base);

#endif
