#ifndef SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_
#define SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

// Create a thread managed via ptrace only.
Thread* threadptraceonly_new(Host* host, Process* process, gint threadID);

void threadptrace_detach(Thread* base);

// Set whether or not ptrace will allow the shim to perform native syscalls.
void threadptrace_setAllowNativeSyscalls(Thread* base, bool is_allowed);

#endif
