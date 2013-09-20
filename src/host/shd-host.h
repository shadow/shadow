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

Host* host_new(GQuark id, gchar* hostname, gchar* requestedIP, gchar* requestedCluster,
		guint64 requestedBWDownKiBps, guint64 requestedBWUpKiBps,
		guint cpuFrequency, gint cpuThreshold, gint cpuPrecision, guint nodeSeed,
		SimulationTime heartbeatInterval, GLogLevelFlags heartbeatLogLevel, gchar* heartbeatLogInfo,
		GLogLevelFlags logLevel, gboolean logPcap, gchar* pcapDir, gchar* qdisc,
		guint64 receiveBufferSize, gboolean autotuneReceiveBuffer,
		guint64 sendBufferSize, gboolean autotuneSendBuffer,
		guint64 interfaceReceiveLength);
void host_free(Host* host, gpointer userData);

void host_lock(Host* host);
void host_unlock(Host* host);

EventQueue* host_getEvents(Host* host);

void host_addApplication(Host* host, GQuark pluginID, gchar* pluginPath,
		SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void host_startApplication(Host* host, Application* application);
void host_stopApplication(Host* host, Application* application);
void host_freeAllApplications(Host* host, gpointer userData);

gint host_compare(gconstpointer a, gconstpointer b, gpointer user_data);
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

gint host_epollControl(Host* host, gint epollDescriptor, gint operation,
		gint fileDescriptor, struct epoll_event* event);
gint host_epollGetEvents(Host* host, gint handle, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents);

gint host_bindToInterface(Host* host, gint handle, in_addr_t bindAddress, in_port_t bindPort);
gint host_connectToPeer(Host* host, gint handle, in_addr_t peerAddress, in_port_t peerPort, sa_family_t family);
gint host_listenForPeer(Host* host, gint handle, gint backlog);
gint host_acceptNewPeer(Host* host, gint handle, in_addr_t* ip, in_port_t* port, gint* acceptedHandle);
gint host_sendUserData(Host* host, gint handle, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_addr_t port, gsize* bytesCopied);
gint host_receiveUserData(Host* host, gint handle, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port, gsize* bytesCopied);
gint host_getPeerName(Host* host, gint handle, in_addr_t* ip, in_port_t* port);
gint host_getSocketName(Host* host, gint handle, in_addr_t* ip, in_port_t* port);

Tracker* host_getTracker(Host* host);
GLogLevelFlags host_getLogLevel(Host* host);
gchar host_isLoggingPcap(Host *host);

#endif /* SHD_HOST_H_ */
