#ifndef SHD_SHIM_SHMEM_H_
#define SHD_SHIM_SHMEM_H_

#include <glib.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

// Shared memory between the shim and Shadow for the host, process, and thread.
typedef struct _ShimShmemHost ShimShmemHost;
typedef struct _ShimShmemProcess ShimShmemProcess;
typedef struct _ShimShmemThread ShimShmemThread;

// Host-wide lock required for some operations.
typedef struct _ShimHostProtectedSharedMem ShimShmemHostLock;

#include "ipc.h"
#include "shim_helper.h"
#include "main/core/support/definitions.h"
#include "shim_event.h"

// Data structures kept in memory shared between Shadow and its managed processes.
//
// Keeping state in these structures allows the shim to access it cheaply,
// including implementing some syscalls on the shim-side without needing to
// transfer control to Shadow.
//
// Most of the state is protected by a per-host lock, which shouldn't be held
// when control may be transferred between Shadow and any managed thread in the
// relevant Host. In the shim this means it shouldn't be held at any point where
// a syscall could be made. Such errors will be caught at run time in debug builds.
//
// Methods that require the host lock to be held take a ShimShmemHostLock
// parameter to enforce that the lock is held. Methods that don't take a lock
// parameter are still thread-safe, and internally use atomics.

size_t shimshmemhost_size();
void shimshmemhost_init(ShimShmemHost* hostMem, GQuark hostId, bool modelUnblockedSyscallLatency,
                        CSimulationTime maxUnappliedCpuLatency,
                        CSimulationTime unblockedSyscallLatency,
                        CSimulationTime unblockedVdsoLatency);
void shimshmemhost_destroy(ShimShmemHost* hostMem);

ShimShmemHostLock* shimshmemhost_lock(ShimShmemHost* host);

// Release and nullify `protected`.
void shimshmemhost_unlock(ShimShmemHost* host, ShimShmemHostLock** protected);

size_t shimshmemprocess_size();
void shimshmemprocess_init(ShimShmemProcess* processMem, GQuark hostId);

// Get and set the emulated time.
CEmulatedTime shimshmem_getEmulatedTime(ShimShmemHost* hostMem);
void shimshmem_setEmulatedTime(ShimShmemHost* hostMem, CEmulatedTime t);

// Get and set the *max* emulated time to which the current time can be incremented.
// Moving time beyond this value requires the current thread to be rescheduled.
CEmulatedTime shimshmem_getMaxRunaheadTime(ShimShmemHostLock* hostMem);
void shimshmem_setMaxRunaheadTime(ShimShmemHostLock* hostMem, CEmulatedTime t);

// Get and set the process's pending signal set.
shd_kernel_sigset_t shimshmem_getProcessPendingSignals(const ShimShmemHostLock* host,
                                                       const ShimShmemProcess* process);
void shimshmem_setProcessPendingSignals(const ShimShmemHostLock* host, ShimShmemProcess* process,
                                        shd_kernel_sigset_t set);

// Get and set the siginfo for the given signal number. Getting is only valid
// when the signal is pending for the process.
siginfo_t shimshmem_getProcessSiginfo(const ShimShmemHostLock* host,
                                      const ShimShmemProcess* process, int sig);
void shimshmem_setProcessSiginfo(const ShimShmemHostLock* host, ShimShmemProcess* process, int sig,
                                 const siginfo_t* info);

// Get and set the signal action for the specified signal.
struct shd_kernel_sigaction shimshmem_getSignalAction(const ShimShmemHostLock* host,
                                                      const ShimShmemProcess* process, int sig);
void shimshmem_setSignalAction(const ShimShmemHostLock* host, ShimShmemProcess* m, int sig,
                               const struct shd_kernel_sigaction* action);

size_t shimshmemthread_size();
void shimshmemthread_init(ShimShmemThread* threadMem, GQuark hostId);

bool shimshmem_getPtraceAllowNativeSyscalls(ShimShmemThread* thread);
void shimshmem_setPtraceAllowNativeSyscalls(ShimShmemThread* thread, bool allow);

// Get and set the thread's pending signal set.
shd_kernel_sigset_t shimshmem_getThreadPendingSignals(const ShimShmemHostLock* host,
                                                      const ShimShmemThread* thread);
void shimshmem_setThreadPendingSignals(const ShimShmemHostLock* host, ShimShmemThread* thread,
                                       shd_kernel_sigset_t sigset);

// Get and set the siginfo for the given signal number. Getting is only valid
// when the signal is pending for the thread.
siginfo_t shimshmem_getThreadSiginfo(const ShimShmemHostLock* host, const ShimShmemThread* thread,
                                     int sig);
void shimshmem_setThreadSiginfo(const ShimShmemHostLock* host, ShimShmemThread* thread, int sig,
                                const siginfo_t* info);

// Get and set the set of blocked signals for the thread.
shd_kernel_sigset_t shimshmem_getBlockedSignals(const ShimShmemHostLock* host,
                                                const ShimShmemThread* m);
void shimshmem_setBlockedSignals(const ShimShmemHostLock* host, ShimShmemThread* thread,
                                 shd_kernel_sigset_t sigset);

// Get and set signal stack as set by `sigaltstack(2)`.
stack_t shimshmem_getSigAltStack(const ShimShmemHostLock* host, const ShimShmemThread* thread);
void shimshmem_setSigAltStack(const ShimShmemHostLock* host, ShimShmemThread* thread,
                              stack_t stack);

// Takes a pending unblocked signal (at the thread or process level) and marks it
// no longer pending. Sets `info` if non-NULL.
//
// Returns 0 if no unblocked signal is pending.
int shimshmem_takePendingUnblockedSignal(const ShimShmemHostLock* lock, ShimShmemProcess* process,
                                         ShimShmemThread* thread, siginfo_t* info);

// Track the number of consecutive unblocked syscalls.
void shimshmem_incrementUnappliedCpuLatency(ShimShmemHostLock* host, CSimulationTime dt);
CSimulationTime shimshmem_getUnappliedCpuLatency(ShimShmemHostLock* host);
void shimshmem_resetUnappliedCpuLatency(ShimShmemHostLock* host);

// Get whether to model latency of unblocked syscalls.
bool shimshmem_getModelUnblockedSyscallLatency(ShimShmemHost* host);

// Get the configured maximum unmber of unblocked syscalls to execute before
// yielding.
uint32_t shimshmem_maxUnappliedCpuLatency(ShimShmemHost* host);

// Get the configured latency to emulate for each unblocked syscall.
CSimulationTime shimshmem_unblockedSyscallLatency(ShimShmemHost* host);

// Get the configured latency to emulate for each unblocked vdso "syscall".
CSimulationTime shimshmem_unblockedVdsoLatency(ShimShmemHost* host);

// Handle SHD_SHIM_EVENT_CLONE_REQ
void shim_shmemHandleClone(const ShimEvent* ev);

// Handle SHD_SHIM_EVENT_CLONE_STRING_REQ
void shim_shmemHandleCloneString(const ShimEvent* ev);

// Handle SHD_SHIM_EVENT_WRITE_REQ
void shim_shmemHandleWrite(const ShimEvent* ev);

// Notify Shadow that a shared memory event has been handled.
void shim_shmemNotifyComplete(struct IPCData* data);

#endif // SHD_SHIM_SHMEM_H_
