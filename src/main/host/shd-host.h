/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_HOST_H_
#define SHD_HOST_H_

#include "shadow.h"

#include <netinet/in.h>

typedef struct _Host Host;

typedef struct _HostParameters HostParameters;
struct _HostParameters {
    GQuark id;
    guint nodeSeed;
    gchar* hostname;
    gchar* ipHint;
    gchar* geocodeHint;
    gchar* typeHint;
    guint64 requestedBWDownKiBps;
    guint64 requestedBWUpKiBps;
    guint64 cpuFrequency;
    guint64 cpuThreshold;
    guint64 cpuPrecision;
    SimulationTime heartbeatInterval;
    GLogLevelFlags heartbeatLogLevel;
    LogInfoFlags heartbeatLogInfo;
    GLogLevelFlags logLevel;
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

void host_boot(Host* host);

void host_addApplication(Host* host, GQuark pluginID,
        SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void host_freeAllApplications(Host* host);

gint host_compare(gconstpointer a, gconstpointer b, gpointer user_data);
GQuark host_getID(Host* host);
gboolean host_isEqual(Host* a, Host* b);
CPU* host_getCPU(Host* host);
gchar* host_getName(Host* host);
Address* host_getDefaultAddress(Host* host);
in_addr_t host_getDefaultIP(Host* host);
gchar* host_getDefaultIPName(Host* host);
Random* host_getRandom(Host* host);
gdouble host_getNextPacketPriority(Host* host);

gboolean host_autotuneReceiveBuffer(Host* host);
gboolean host_autotuneSendBuffer(Host* host);

gint host_createDescriptor(Host* host, DescriptorType type);
void host_closeDescriptor(Host* host, gint handle);
gint host_closeUser(Host* host, gint handle);
Descriptor* host_lookupDescriptor(Host* host, gint handle);
NetworkInterface* host_lookupInterface(Host* host, in_addr_t handle);

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
GLogLevelFlags host_getLogLevel(Host* host);

const gchar* host_getDataPath(Host* host);

#endif /* SHD_HOST_H_ */
