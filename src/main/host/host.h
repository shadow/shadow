/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_HOST_H_
#define SHD_HOST_H_

#include <glib.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/select.h>
#include <sys/socket.h>

typedef struct _HostCInternal HostCInternal;

#include "lib/logger/log_level.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/tsc/tsc.h"
#include "main/core/support/definitions.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/futex_table.h"
#include "main/host/protocol.h"
#include "main/host/thread.h"
#include "main/host/tracker_types.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"

HostCInternal* hostc_new(HostId id, const char* hostName);
void hostc_unref(HostCInternal* host);

void hostc_continueExecutionTimer(HostCInternal* host);
void hostc_stopExecutionTimer(HostCInternal* host);

void hostc_setup(const Host* host);
void hostc_boot(const Host* rhost);
void hostc_shutdown(const Host* rhost);

void hostc_addApplication(const Host* host, CSimulationTime startTime, CSimulationTime stopTime,
                          const gchar* pluginName, const gchar* pluginPath,
                          const gchar* const* envv, const gchar* const* argv,
                          bool pause_for_debugging);
void hostc_freeAllApplications(const Host* rhost);

Tsc* hostc_getTsc(HostCInternal* host);

Router* hostc_getUpstreamRouter(HostCInternal* host);

Tracker* hostc_getTracker(HostCInternal* host);

in_port_t hostc_getRandomFreePort(const Host* host, ProtocolType type, in_addr_t interfaceIP,
                                  in_addr_t peerIP, in_port_t peerPort);

FutexTable* hostc_getFutexTable(HostCInternal* host);

// converts a virtual (shadow) tid into the native tid
pid_t hostc_getNativeTID(HostCInternal* host, pid_t virtualPID, pid_t virtualTID);

// Returns the specified process, or NULL if it doesn't exist.
Process* hostc_getProcess(HostCInternal* host, pid_t virtualPID);

// Returns the specified thread, or NULL if it doesn't exist.
// If you already have the thread's Process*, `process_getThread` may be more
// efficient.
Thread* hostc_getThread(HostCInternal* host, pid_t virtualTID);

// Returns host-specific state that's kept in memory shared with the shim(s).
ShimShmemHost* hostc_getSharedMem(HostCInternal* host);

// Returns the lock, or NULL if the lock isn't held by Shadow.
//
// Generally the lock can and should be held when Shadow is running, and *not*
// held when any of the host's managed threads are running (leaving it available
// to be taken by the shim). While this can be a little fragile to ensure
// properly, debug builds detect if we get it wrong (e.g. we try accessing
// protected data without holding the lock, or the shim tries to take the lock
// but can't).
ShimShmemHostLock* hostc_getShimShmemLock(HostCInternal* host);

// Take the host's shared memory lock. See `hostc_getShimShmemLock`.
void hostc_lockShimShmemLock(HostCInternal* host);

// Release the host's shared memory lock. See `hostc_getShimShmemLock`.
void hostc_unlockShimShmemLock(HostCInternal* host);

// Returns the next value and increments our monotonically increasing
// determinism sequence counter. The resulting values can be sorted to
// established a deterministic ordering, which can be useful when iterating
// items that are otherwise inconsistently ordered (e.g. hash table iterators).
guint64 hostc_getNextDeterministicSequenceValue(HostCInternal* host);

// Schedule a task for this host at time 'time'.
gboolean hostc_scheduleTaskAtEmulatedTime(const Host* host, TaskRef* task, CEmulatedTime time);
// Schedule a task for this host at a time 'nanoDelay' from now,.
gboolean hostc_scheduleTaskWithDelay(const Host* host, TaskRef* task, CSimulationTime nanoDelay);

#endif /* SHD_hostc_H_ */
