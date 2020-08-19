#ifndef SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_
#define SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

Thread* threadptrace_new(Host* host, Process* process, gint threadID);
void threadptrace_detach(Thread* base);

#endif
