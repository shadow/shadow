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

#include "core/logger/shd-log-level.h"
#include "core/support/shd-definitions.h"
#include "core/support/shd-options.h"
#include "host/descriptor/shd-descriptor.h"
#include "host/shd-cpu.h"
#include "host/shd-network-interface.h"
#include "host/shd-tracker.h"
#include "routing/shd-address.h"
#include "routing/shd-dns.h"
#include "routing/shd-router.h"
#include "routing/shd-topology.h"
#include "utility/shd-random.h"

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
void host_addApplication(Host* host, SimulationTime startTime, SimulationTime stopTime,
        const gchar* pluginName, const gchar* pluginPath, const gchar* pluginSymbol,
        const gchar* preloadName, const gchar* preloadPath, gchar* arguments);
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

gint host_createDescriptor(Host* host, DescriptorType type);
void host_closeDescriptor(Host* host, gint handle);
gint host_closeUser(Host* host, gint handle);
gint host_shutdownSocket(Host* host, gint handle, gint how);
Descriptor* host_lookupDescriptor(Host* host, gint handle);
NetworkInterface* host_lookupInterface(Host* host, in_addr_t handle);
Router* host_getUpstreamRouter(Host* host, in_addr_t handle);

void host_returnHandleHack(gint handle);
gboolean host_isShadowDescriptor(Host* host, gint handle);
gint host_createShadowHandle(Host* host, gint osHandle);
gint host_getOSHandle(Host* host, gint shadowHandle);
gint host_getShadowHandle(Host* host, gint osHandle);
void host_setRandomHandle(Host* host, gint handle);
gboolean host_isRandomHandle(Host* host, gint handle);
void host_destroyShadowHandle(Host* host, gint shadowHandle);

gint host_epollControl(Host* host, gint epollDescriptor, gint operation,
        gint fileDescriptor, struct epoll_event* event);
gint host_epollGetEvents(Host* host, gint handle, struct epoll_event* eventArray,
        gint eventArrayLength, gint* nEvents);
gint host_select(Host* host, fd_set* readable, fd_set* writeable, fd_set* erroneous);
gint host_poll(Host* host, struct pollfd *pollFDs, nfds_t numPollFDs);

gint host_bindToInterface(Host* host, gint handle, const struct sockaddr* address);
gint host_connectToPeer(Host* host, gint handle, const struct sockaddr* address);
gint host_listenForPeer(Host* host, gint handle, gint backlog);
gint host_acceptNewPeer(Host* host, gint handle, in_addr_t* ip, in_port_t* port, gint* acceptedHandle);
gint host_sendUserData(Host* host, gint handle, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_addr_t port, gsize* bytesCopied);
gint host_receiveUserData(Host* host, gint handle, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port, gsize* bytesCopied);
gint host_getPeerName(Host* host, gint handle, const struct sockaddr* address, socklen_t* len);
gint host_getSocketName(Host* host, gint handle, const struct sockaddr* address, socklen_t* len);

Tracker* host_getTracker(Host* host);
LogLevel host_getLogLevel(Host* host);

const gchar* host_getDataPath(Host* host);

void host_migrate(Host* host, pthread_t* from, pthread_t* to);

#endif /* SHD_HOST_H_ */
