#ifndef SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_
#define SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_

#include "main/host/syscall_handler.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"

typedef struct _ThreadPreload ThreadPreload;

Thread* threadpreload_new(Host* host, Process* process, gint threadID);

#endif // SRC_MAIN_HOST_SHD_THREAD_PRELOAD_H_
