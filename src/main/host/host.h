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

#include "main/core/support/definitions.h"
#include "main/core/support/options.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/network_interface.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/router.h"
#include "main/routing/topology.h"
#include "main/utility/random.h"
#include "support/logger/log_level.h"

typedef struct _Host Host;

typedef struct _HostParameters HostParameters;
struct _HostParameters {
    GQuark id;
    guint nodeSeed;
    gchar* hostname;
    gchar* ipHint;
    gchar* citycodeHint;
    gchar* countrycodeHint;
    gchar* geocodeHint;
    gchar* typeHint;
    guint64 requestedBWDownKiBps;
    guint64 requestedBWUpKiBps;
    guint64 cpuFrequency;
    guint64 cpuThreshold;
    guint64 cpuPrecision;
    SimulationTime heartbeatInterval;
    LogLevel heartbeatLogLevel;
    LogInfoFlags heartbeatLogInfo;
    LogLevel logLevel;
    gboolean logPcap;
    gchar* pcapDir;
    QDiscMode qdisc;
    guint64 recvBufSize;
    gboolean autotuneRecvBuf;
    guint64 sendBufSize;
    gboolean autotuneSendBuf;
    guint64 interfaceBufSize;
};

Host* host_new(HostParameters* params);
void host_ref(Host* host);
void host_unref(Host* host);

void host_lock(Host* host);
void host_unlock(Host* host);

void host_continueExecutionTimer(Host* host);
void host_stopExecutionTimer(Host* host);
gdouble host_getElapsedExecutionTime(Host* host);

void host_setup(Host* host, DNS* dns, Topology* topology, guint rawCPUFreq, const gchar* hostRootPath);
void host_boot(Host* host);
void host_shutdown(Host* host);

guint host_getNewProcessID(Host* host);
guint64 host_getNewEventID(Host* host);
guint64 host_getNewPacketID(Host* host);
void host_addApplication(Host* host, SimulationTime startTime,
                         SimulationTime stopTime, InterposeMethod interposeMethod,
                         const gchar* pluginName, const gchar* pluginPath,
                         const gchar* pluginSymbol, gchar** envv, gchar** argv);
void host_freeAllApplications(Host* host);

gint host_compare(gconstpointer a, gconstpointer b, gpointer user_data);
GQuark host_getID(Host* host);
gboolean host_isEqual(Host* a, Host* b);
CPU* host_getCPU(Host* host);
gchar* host_getName(Host* host);
Address* host_getDefaultAddress(Host* host);
in_addr_t host_getDefaultIP(Host* host);
Random* host_getRandom(Host* host);
gdouble host_getNextPacketPriority(Host* host);

gboolean host_autotuneReceiveBuffer(Host* host);
gboolean host_autotuneSendBuffer(Host* host);

Descriptor* host_createDescriptor(Host* host, DescriptorType type);
Descriptor* host_lookupDescriptor(Host* host, gint handle);
void host_closeDescriptor(Host* host, gint handle);

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
void host_associateInterface(Host* host, Socket* socket, in_addr_t bindAddress,
                             in_port_t bindPort, in_addr_t peerAddress,
                             in_port_t peerPort);
in_port_t host_getRandomFreePort(Host* host, ProtocolType type,
                                 in_addr_t interfaceIP, in_addr_t peerIP,
                                 in_port_t peerPort);

#endif /* SHD_HOST_H_ */
