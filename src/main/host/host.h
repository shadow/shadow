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

typedef struct _Host Host;
typedef GQuark HostId;

#include "lib/logger/log_level.h"
#include "lib/shadow-shim-helper-rs/shim_shmem.h"
#include "lib/tsc/tsc.h"
#include "main/core/support/definitions.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/futex_table.h"
#include "main/host/host_parameters.h"
#include "main/host/network_interface.h"
#include "main/host/protocol.h"
#include "main/host/thread.h"
#include "main/host/tracker_types.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/router.h"

Host* host_new(const HostParameters* params);
void host_ref(Host* host);
void host_unref(Host* host);

bool host_pushLocalEvent(Host* host, Event* event);
void host_execute(Host* host, CEmulatedTime until);
CEmulatedTime host_nextEventTime(Host* host);
const ThreadSafeEventQueue* host_getOwnedEventQueue(Host* host);

void host_continueExecutionTimer(Host* host);
void host_stopExecutionTimer(Host* host);

void host_setup(Host* host, DNS* dns, gulong rawCPUFreq, const gchar* hostRootPath);
void host_boot(Host* host);
void host_shutdown(Host* host);

guint host_getNewProcessID(Host* host);
guint64 host_getNewEventID(Host* host);
guint64 host_getNewPacketID(Host* host);
void host_addApplication(Host* host, CSimulationTime startTime, CSimulationTime stopTime,
                         const gchar* pluginName, const gchar* pluginPath, const gchar* const* envv,
                         const gchar* const* argv, bool pause_for_debugging);
void host_freeAllApplications(Host* host);

HostId host_getID(Host* host);
CPU* host_getCPU(Host* host);
Tsc* host_getTsc(Host* host);
const gchar* host_getName(Host* host);
Address* host_getDefaultAddress(Host* host);
in_addr_t host_getDefaultIP(Host* host);
Random* host_getRandom(Host* host);
gdouble host_getNextPacketPriority(Host* host);

gboolean host_autotuneReceiveBuffer(Host* host);
gboolean host_autotuneSendBuffer(Host* host);
guint64 host_getConfiguredRecvBufSize(Host* host);
guint64 host_getConfiguredSendBufSize(Host* host);

NetworkInterface* host_lookupInterface(Host* host, in_addr_t handle);
Router* host_getUpstreamRouter(Host* host, in_addr_t handle);

uint64_t host_get_bw_down_kiBps(Host* host);
uint64_t host_get_bw_up_kiBps(Host* host);

void host_returnHandleHack(gint handle);

Tracker* host_getTracker(Host* host);
LogLevel host_getLogLevel(Host* host);

const gchar* host_getDataPath(Host* host);

gboolean host_doesInterfaceExist(Host* host, in_addr_t interfaceIP);
gboolean host_isInterfaceAvailable(Host* host, ProtocolType type,
                                   in_addr_t interfaceIP, in_port_t port,
                                   in_addr_t peerIP, in_port_t peerPort);
void host_associateInterface(Host* host, const CompatSocket* socket, in_addr_t bindAddress);
void host_disassociateInterface(Host* host, const CompatSocket* socket);
in_port_t host_getRandomFreePort(Host* host, ProtocolType type,
                                 in_addr_t interfaceIP, in_addr_t peerIP,
                                 in_port_t peerPort);

Arc_AtomicRefCell_AbstractUnixNamespace* host_getAbstractUnixNamespace(Host* host);
FutexTable* host_getFutexTable(Host* host);

// converts a virtual (shadow) tid into the native tid
pid_t host_getNativeTID(Host* host, pid_t virtualPID, pid_t virtualTID);

// Returns the specified process, or NULL if it doesn't exist.
Process* host_getProcess(Host* host, pid_t virtualPID);

// Returns the specified thread, or NULL if it doesn't exist.
// If you already have the thread's Process*, `process_getThread` may be more
// efficient.
Thread* host_getThread(Host* host, pid_t virtualTID);

// Returns host-specific state that's kept in memory shared with the shim(s).
ShimShmemHost* host_getSharedMem(Host* host);

// Returns the lock, or NULL if the lock isn't held by Shadow.
//
// Generally the lock can and should be held when Shadow is running, and *not*
// held when any of the host's managed threads are running (leaving it available
// to be taken by the shim). While this can be a little fragile to ensure
// properly, debug builds detect if we get it wrong (e.g. we try accessing
// protected data without holding the lock, or the shim tries to take the lock
// but can't).
ShimShmemHostLock* host_getShimShmemLock(Host* host);

// Take the host's shared memory lock. See `host_getShimShmemLock`.
void host_lockShimShmemLock(Host* host);

// Release the host's shared memory lock. See `host_getShimShmemLock`.
void host_unlockShimShmemLock(Host* host);

// Returns the next value and increments our monotonically increasing
// determinism sequence counter. The resulting values can be sorted to
// established a deterministic ordering, which can be useful when iterating
// items that are otherwise inconsistently ordered (e.g. hash table iterators).
guint64 host_getNextDeterministicSequenceValue(Host* host);

// Schedule a task for this host at time 'time'.
gboolean host_scheduleTaskAtEmulatedTime(Host* host, TaskRef* task, CEmulatedTime time);
// Schedule a task for this host at a time 'nanoDelay' from now,.
gboolean host_scheduleTaskWithDelay(Host* host, TaskRef* task, CSimulationTime nanoDelay);

#endif /* SHD_HOST_H_ */
