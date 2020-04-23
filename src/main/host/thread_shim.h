#ifndef SRC_MAIN_HOST_SHD_THREAD_SHIM_H_
#define SRC_MAIN_HOST_SHD_THREAD_SHIM_H_

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

typedef struct _ThreadShim ThreadShim;

Thread* threadshim_new(Host* host, Process* process, gint threadID);

#endif
