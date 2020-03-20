#ifndef SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_
#define SRC_MAIN_HOST_SHD_THREAD_PTRACE_H_

#include "main/host/shd-syscall-handler.h"
#include "main/host/shd-syscall-types.h"
#include "main/host/shd-thread.h"

Thread* threadptrace_new(gint threadID, SysCallHandler* sys);

#endif
