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

#include "lib/logger/log_level.h"
#include "lib/shim/shim_shmem.h"
#include "lib/tsc/tsc.h"
#include "main/core/support/definitions.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/futex_table.h"
#include "main/host/host_parameters.h"
#include "main/host/network_interface.h"
#include "main/host/thread.h"
#include "main/host/tracker_types.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/router.h"

Host* host_new(HostParameters* params);
void host_ref(Host* host);
void host_unref(Host* host);

void host_lock(Host* host);
void host_unlock(Host* host);

#ifdef USE_PERF_TIMERS
void host_continueExecutionTimer(Host* host);
void host_stopExecutionTimer(Host* host);
#else
// define macros that do nothing
#define host_continueExecutionTimer(host)
#define host_stopExecutionTimer(host)
#endif

void host_setup(Host* host, DNS* dns, guint rawCPUFreq, const gchar* hostRootPath);
void host_boot(Host* host);
void host_shutdown(Host* host);

guint host_getNewProcessID(Host* host);
guint64 host_getNewEventID(Host* host);
guint64 host_getNewPacketID(Host* host);
void host_addApplication(Host* host, SimulationTime startTime, SimulationTime stopTime,
                         InterposeMethod interposeMethod, const gchar* pluginName,
                         const gchar* pluginPath, gchar** envv, gchar** argv,
                         bool pause_for_debugging);
void host_detachAllPlugins(Host* host);
void host_freeAllApplications(Host* host);

gint host_compare(gconstpointer a, gconstpointer b, gpointer user_data);
GQuark host_getID(Host* host);
gboolean host_isEqual(Host* a, Host* b);
CPU* host_getCPU(Host* host);
Tsc* host_getTsc(Host* host);
gchar* host_getName(Host* host);
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

#endif /* SHD_HOST_H_ */
