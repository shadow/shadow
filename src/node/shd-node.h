/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_NODE_H_
#define SHD_NODE_H_

#include "shadow.h"

#include <netinet/in.h>

typedef struct _Node Node;

Node* node_new(GQuark id, Network* network, guint32 ip,
		GString* hostname, guint64 bwDownKiBps, guint64 bwUpKiBps, guint cpuFrequency, gint cpuThreshold, gint cpuPrecision,
		guint nodeSeed, SimulationTime heartbeatInterval, GLogLevelFlags heartbeatLogLevel, gchar* heartbeatLogInfo,
		GLogLevelFlags logLevel, gboolean logPcap, gchar* pcapDir, gchar* qdisc,
		guint64 receiveBufferSize, guint64 sendBufferSize, guint64 interfaceReceiveLength);
void node_free(Node* node, gpointer userData);

void node_lock(Node* node);
void node_unlock(Node* node);

EventQueue* node_getEvents(Node* node);

void node_addApplication(Node* node, GQuark pluginID, gchar* pluginPath,
		SimulationTime startTime, SimulationTime stopTime, gchar* arguments);
void node_startApplication(Node* node, Application* application);
void node_stopApplication(Node* node, Application* application);
void node_freeAllApplications(Node* node, gpointer userData);

gint node_compare(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean node_isEqual(Node* a, Node* b);
CPU* node_getCPU(Node* node);
Network* node_getNetwork(Node* node);
gchar* node_getName(Node* node);
in_addr_t node_getDefaultIP(Node* node);
gchar* node_getDefaultIPName(Node* node);
Random* node_getRandom(Node* node);
gdouble node_getNextPacketPriority(Node* node);

gint node_createDescriptor(Node* node, enum DescriptorType type);
void node_closeDescriptor(Node* node, gint handle);
gint node_closeUser(Node* node, gint handle);
Descriptor* node_lookupDescriptor(Node* node, gint handle);
NetworkInterface* node_lookupInterface(Node* node, in_addr_t handle);

gint node_epollControl(Node* node, gint epollDescriptor, gint operation,
		gint fileDescriptor, struct epoll_event* event);
gint node_epollGetEvents(Node* node, gint handle, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents);

gint node_bindToInterface(Node* node, gint handle, in_addr_t bindAddress, in_port_t bindPort);
gint node_connectToPeer(Node* node, gint handle, in_addr_t peerAddress, in_port_t peerPort, sa_family_t family);
gint node_listenForPeer(Node* node, gint handle, gint backlog);
gint node_acceptNewPeer(Node* node, gint handle, in_addr_t* ip, in_port_t* port, gint* acceptedHandle);
gint node_sendUserData(Node* node, gint handle, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_addr_t port, gsize* bytesCopied);
gint node_receiveUserData(Node* node, gint handle, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port, gsize* bytesCopied);
gint node_getPeerName(Node* node, gint handle, in_addr_t* ip, in_port_t* port);
gint node_getSocketName(Node* node, gint handle, in_addr_t* ip, in_port_t* port);

Tracker* node_getTracker(Node* node);
GLogLevelFlags node_getLogLevel(Node* node);
gchar node_isLoggingPcap(Node *node);

#endif /* SHD_NODE_H_ */
