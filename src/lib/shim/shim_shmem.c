#include "lib/shim/shim_shmem.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "lib/logger/logger.h"
#include "lib/shim/ipc.h"
#include "lib/shim/shim_event.h"
#include "main/host/host.h"
#include "main/shmem/shmem_allocator.h"

struct _ShimHostProtectedSharedMem {
    GQuark host_id;

    // Number of syscalls that have executed without blocking.
    uint32_t unblocked_syscall_count;
};

struct _ShimShmemHost {
    GQuark host_id;

    // The host lock. Guards _ShimShmemHost.protected,
    // _ShimShmemProcess.protected, and _ShimShmemThread.protected.
    pthread_mutex_t mutex;

    // Guarded by `mutex`.
    ShimShmemHostLock protected;

    // Number of syscalls allowed to execute before yielding.
    // TODO: Move to a "ShimShmemGlobal" struct if we make one, and if this
    // stays a global constant; Or down into the process if we make it a
    // per-process option.
    //
    // Thread Safety: immutable after initialization.
    const uint32_t unblocked_syscall_limit;

    // Current simulation time.
    _Atomic EmulatedTime sim_time;

    // Max simulation time to which sim_time may be incremented.  Moving time
    // beyond this value requires the current thread to be rescheduled.
    _Atomic EmulatedTime max_sim_time;
};

typedef struct _ShimProcessProtectedSharedMem ShimProcessProtectedSharedMem;
struct _ShimProcessProtectedSharedMem {
    GQuark host_id;

    // Process-directed pending signals.
    shd_kernel_sigset_t pending_signals;

    // siginfo for each of the standard signals.
    siginfo_t pending_standard_siginfos[SHD_STANDARD_SIGNAL_MAX_NO];

    // actions for both standard and realtime signals.
    // We currently support configuring handlers for realtime signals, but not
    // actually delivering them. This is to handle the case where handlers are
    // defensively installed, but not used in practice.
    struct shd_kernel_sigaction signal_actions[SHD_SIGRT_MAX];
};

struct _ShimShmemProcess {
    GQuark host_id;

    // Guarded by ShimShmemHost.mutex.
    ShimProcessProtectedSharedMem protected;
};

typedef struct _ShimThreadProtectedSharedMem ShimThreadProtectedSharedMem;
struct _ShimThreadProtectedSharedMem {
    GQuark host_id;

    // Thread-directed pending signals.
    shd_kernel_sigset_t pending_signals;

    // siginfo for each of the 32 standard signals.
    siginfo_t pending_standard_siginfos[SHD_STANDARD_SIGNAL_MAX_NO];

    // Signal mask, e.g. as set by `sigprocmask`.
    // We don't use sigset_t since glibc uses a much larger bitfield than
    // actually supported by the kernel.
    shd_kernel_sigset_t blocked_signals;

    // Configured alternate signal stack for this thread.
    stack_t sigaltstack;
};

struct _ShimShmemThread {
    GQuark host_id;

    // While true, Shadow allows syscalls to be executed natively.
    atomic_bool ptrace_allow_native_syscalls;

    // Guarded by ShimShmemHost.mutex.
    ShimThreadProtectedSharedMem protected;
};

size_t shimshmemhost_size() { return sizeof(ShimShmemHost); }

void shimshmemhost_init(ShimShmemHost* hostMem, Host* host, uint32_t unblockedSyscallLimit) {
    assert(hostMem);
    // We use `memcpy` instead of struct assignment here to allow us to
    // initialize the const members of `hostMem`.
    memcpy(hostMem,
           &(ShimShmemHost){
               .host_id = host_getID(host),
               .mutex = PTHREAD_MUTEX_INITIALIZER,
               .unblocked_syscall_limit = unblockedSyscallLimit,
               .protected =
                   {
                       .host_id = host_getID(host),
                   },
           },
           sizeof(ShimShmemHost));
}

void shimshmemhost_destroy(ShimShmemHost* hostMem) {
    assert(hostMem);
    pthread_mutex_destroy(&hostMem->mutex);
}

void shimshmem_incrementUnblockedSyscallCount(ShimShmemHostLock* host) {
    assert(host);
    ++host->unblocked_syscall_count;
}

uint32_t shimshmem_getUnblockedSyscallCount(ShimShmemHostLock* host) {
    assert(host);
    return host->unblocked_syscall_count;
}

uint32_t shimshmem_unblockedSyscallLimit(ShimShmemHost* host) {
    assert(host);
    return host->unblocked_syscall_limit;
}

void shimshmem_resetUnblockedSyscallCount(ShimShmemHostLock* host) {
    assert(host);
    host->unblocked_syscall_count = 0;
}

shd_kernel_sigset_t shimshmem_getProcessPendingSignals(const ShimShmemHostLock* host,
                                                       const ShimShmemProcess* process) {
    assert(host);
    assert(process);
    assert(host->host_id == process->host_id);
    return process->protected.pending_signals;
}

void shimshmem_setProcessPendingSignals(const ShimShmemHostLock* host, ShimShmemProcess* process,
                                        shd_kernel_sigset_t set) {
    assert(host);
    assert(process);
    assert(host->host_id == process->host_id);
    process->protected.pending_signals = set;
}

siginfo_t shimshmem_getProcessSiginfo(const ShimShmemHostLock* host,
                                      const ShimShmemProcess* process, int sig) {
    assert(host);
    assert(process);
    assert(host->host_id == process->host_id);
    assert(sig >= 1);
    assert(sig <= SHD_STANDARD_SIGNAL_MAX_NO);
    return process->protected.pending_standard_siginfos[sig - 1];
}

void shimshmem_setProcessSiginfo(const ShimShmemHostLock* host, ShimShmemProcess* process, int sig,
                                 const siginfo_t* info) {
    assert(host);
    assert(process);
    assert(host->host_id == process->host_id);
    assert(sig >= 1);
    assert(sig <= SHD_STANDARD_SIGNAL_MAX_NO);
    process->protected.pending_standard_siginfos[sig - 1] = *info;
}

struct shd_kernel_sigaction shimshmem_getSignalAction(const ShimShmemHostLock* host,
                                                      const ShimShmemProcess* process, int sig) {
    assert(host);
    assert(process);
    assert(host->host_id == process->host_id);
    assert(sig >= 1);
    assert(sig <= SHD_SIGRT_MAX);
    return process->protected.signal_actions[sig - 1];
}

void shimshmem_setSignalAction(const ShimShmemHostLock* host, ShimShmemProcess* process, int sig,
                               const struct shd_kernel_sigaction* action) {
    assert(host);
    assert(process);
    assert(host->host_id == process->host_id);
    assert(sig >= 1);
    assert(sig <= SHD_SIGRT_MAX);
    process->protected.signal_actions[sig - 1] = *action;
}

size_t shimshmemprocess_size() { return sizeof(ShimShmemProcess); }

void shimshmemprocess_init(ShimShmemProcess* processMem, Process* process) {
    *processMem = (ShimShmemProcess){
        .host_id = process_getHostId(process),
        .protected =
            {
                .host_id = process_getHostId(process),
            },
    };
}

EmulatedTime shimshmem_getEmulatedTime(ShimShmemHost* hostMem) {
    assert(hostMem);
    return atomic_load(&hostMem->sim_time);
}

void shimshmem_setEmulatedTime(ShimShmemHost* hostMem, EmulatedTime t) {
    assert(hostMem);
    assert(t <= shimshmem_getMaxEmulatedTime(hostMem));
    atomic_store(&hostMem->sim_time, t);
}

EmulatedTime shimshmem_getMaxEmulatedTime(ShimShmemHost* hostMem) {
    assert(hostMem);
    return atomic_load(&hostMem->max_sim_time);
}

void shimshmem_setMaxEmulatedTime(ShimShmemHost* hostMem, EmulatedTime t) {
    assert(hostMem);
    atomic_store(&hostMem->max_sim_time, t);
}

shd_kernel_sigset_t shimshmem_getThreadPendingSignals(const ShimShmemHostLock* host,
                                                      const ShimShmemThread* thread) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    return thread->protected.pending_signals;
}

void shimshmem_setThreadPendingSignals(const ShimShmemHostLock* host, ShimShmemThread* thread,
                                       shd_kernel_sigset_t sigset) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    thread->protected.pending_signals = sigset;
}

siginfo_t shimshmem_getThreadSiginfo(const ShimShmemHostLock* host, const ShimShmemThread* thread,
                                     int sig) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    assert(sig >= 1);
    assert(sig <= SHD_STANDARD_SIGNAL_MAX_NO);
    return thread->protected.pending_standard_siginfos[sig - 1];
}

void shimshmem_setThreadSiginfo(const ShimShmemHostLock* host, ShimShmemThread* thread, int sig,
                                const siginfo_t* info) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    assert(sig >= 1);
    assert(sig <= SHD_STANDARD_SIGNAL_MAX_NO);
    thread->protected.pending_standard_siginfos[sig - 1] = *info;
}

stack_t shimshmem_getSigAltStack(const ShimShmemHostLock* host, const ShimShmemThread* thread) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    return thread->protected.sigaltstack;
}

void shimshmem_setSigAltStack(const ShimShmemHostLock* host, ShimShmemThread* thread,
                              stack_t stack) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    thread->protected.sigaltstack = stack;
}

shd_kernel_sigset_t shimshmem_getBlockedSignals(const ShimShmemHostLock* host,
                                                const ShimShmemThread* thread) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    return thread->protected.blocked_signals;
}

void shimshmem_setBlockedSignals(const ShimShmemHostLock* host, ShimShmemThread* thread,
                                 shd_kernel_sigset_t sigset) {
    assert(host);
    assert(thread);
    assert(host->host_id == thread->host_id);
    thread->protected.blocked_signals = sigset;
}

size_t shimshmemthread_size() { return sizeof(ShimShmemThread); }

void shimshmemthread_init(ShimShmemThread* threadMem, Thread* thread) {
    *threadMem = (ShimShmemThread){
        .host_id = thread_getHostId(thread),
        .protected =
            {
                .host_id = thread_getHostId(thread),
                .sigaltstack.ss_flags = SS_DISABLE,
            },
    };
}

bool shimshmem_getPtraceAllowNativeSyscalls(ShimShmemThread* thread) {
    return atomic_load(&thread->ptrace_allow_native_syscalls);
}

void shimshmem_setPtraceAllowNativeSyscalls(ShimShmemThread* thread, bool allow) {
    atomic_store(&thread->ptrace_allow_native_syscalls, allow);
}

ShimShmemHostLock* shimshmemhost_lock(ShimShmemHost* host) {
    assert(host);
    if (pthread_mutex_trylock(&host->mutex) != 0) {
        // This is likely a deadlock.
        panic("Lock is already held. This is probably a deadlock.");
    }
    return &host->protected;
}
void shimshmemhost_unlock(ShimShmemHost* host, ShimShmemHostLock** protected) {
    assert(host);
    assert(protected);
    assert(*protected);
    assert(host->host_id == (*protected)->host_id);

    *protected = NULL;
    int rv;
    if ((rv = pthread_mutex_unlock(&host->mutex)) != 0) {
        panic("pthread_mutex_unlock: %s", strerror(rv));
    }
}

static int _shimshmem_takePendingUnblockedThreadSignal(const ShimShmemHostLock* lock,
                                                       shd_kernel_sigset_t unblockedSignals,
                                                       ShimShmemThread* thread, siginfo_t* info) {
    shd_kernel_sigset_t thread_pending_signals = shimshmem_getThreadPendingSignals(lock, thread);
    shd_kernel_sigset_t thread_pending_unblocked_signals =
        shd_sigandset(&thread_pending_signals, &unblockedSignals);
    if (!shd_sigisemptyset(&thread_pending_unblocked_signals)) {
        int signo = shd_siglowest(&thread_pending_signals);
        if (info) {
            *info = shimshmem_getThreadSiginfo(lock, thread, signo);
        }
        shd_sigdelset(&thread_pending_signals, signo);
        shimshmem_setThreadPendingSignals(lock, thread, thread_pending_signals);
        return signo;
    }
    return 0;
}

static int _shimshmem_takePendingUnblockedProcessSignal(const ShimShmemHostLock* lock,
                                                        shd_kernel_sigset_t unblockedSignals,
                                                        ShimShmemProcess* process,
                                                        siginfo_t* info) {
    shd_kernel_sigset_t process_pending_signals = shimshmem_getProcessPendingSignals(lock, process);
    shd_kernel_sigset_t process_pending_unblocked_signals =
        shd_sigandset(&process_pending_signals, &unblockedSignals);
    if (!shd_sigisemptyset(&process_pending_unblocked_signals)) {
        int signo = shd_siglowest(&process_pending_signals);
        if (info) {
            *info = shimshmem_getProcessSiginfo(lock, process, signo);
        }
        shd_sigdelset(&process_pending_signals, signo);
        shimshmem_setProcessPendingSignals(lock, process, process_pending_signals);
        return signo;
    }
    return 0;
}

int shimshmem_takePendingUnblockedSignal(const ShimShmemHostLock* lock, ShimShmemProcess* process,
                                         ShimShmemThread* thread, siginfo_t* info) {
    shd_kernel_sigset_t unblocked_signals;
    {
        shd_kernel_sigset_t blocked_signals = shimshmem_getBlockedSignals(lock, thread);
        unblocked_signals = shd_signotset(&blocked_signals);
    }

    int signo = _shimshmem_takePendingUnblockedThreadSignal(lock, unblocked_signals, thread, info);
    if (signo != 0) {
        return signo;
    }

    return _shimshmem_takePendingUnblockedProcessSignal(lock, unblocked_signals, process, info);
}

void shim_shmemHandleClone(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHD_SHIM_EVENT_CLONE_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(
        &ev->event_data.shmem_blk.serial);

    memcpy(blk.p, (void*)ev->event_data.shmem_blk.plugin_ptr.val,
           ev->event_data.shmem_blk.n);
}

void shim_shmemHandleCloneString(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHD_SHIM_EVENT_CLONE_STRING_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(
        &ev->event_data.shmem_blk.serial);

    strncpy(blk.p, (void*)ev->event_data.shmem_blk.plugin_ptr.val,
            ev->event_data.shmem_blk.n);
    // TODO: Shrink buffer to what's actually needed?
}

void shim_shmemHandleWrite(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHD_SHIM_EVENT_WRITE_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(
        &ev->event_data.shmem_blk.serial);

    memcpy((void*)ev->event_data.shmem_blk.plugin_ptr.val, blk.p,
           ev->event_data.shmem_blk.n);
}

void shim_shmemNotifyComplete(struct IPCData *data) {
    ShimEvent ev = {
        .event_id = SHD_SHIM_EVENT_SHMEM_COMPLETE,
    };
    shimevent_sendEventToShadow(data, &ev);
}
