#ifndef SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_
#define SRC_MAIN_HOST_SHD_THREAD_PROTECTED_H_

/*
 * Implementation details for the Thread interface.
 *
 * This file should only be included by C files *implementing* the Thread
 * interface.
 */

#include <stdarg.h>

#include "lib/shim/shim_event.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/host/managed_thread.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/utility/utility.h"

struct _Thread {
    // For safe down-casting. Set and checked by child class.
    int type_id;

    int tid;

    pid_t nativePid;
    pid_t nativeTid;
    Host* host;
    Process* process;
    // If non-null, this address should be cleared and futex-awoken on thread exit.
    // See set_tid_address(2).
    PluginPtr tidAddress;
    int referenceCount;

    SysCallHandler* sys;

    ShMemBlock shimSharedMemBlock;

    // Non-null if blocked by a syscall.
    SysCallCondition* cond;

    // Value storing the current CPU affinity of the thread (more preceisely,
    // of the native thread backing this thread object). This value will be set
    // to AFFINITY_UNINIT if CPU pinning is not enabled or if the thread has
    // not yet been pinned to a CPU.
    int affinity;

    // The native, managed thread
    ManagedThread* mthread;

    MAGIC_DECLARE;
};

Thread thread_create(Host* host, Process* process, int type_id, int threadID);

#endif
