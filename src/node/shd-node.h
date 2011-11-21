/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SHD_NODE_H_
#define SHD_NODE_H_

#include "shadow.h"

#include <netinet/in.h>

typedef struct _Node Node;

Node* node_new(GQuark id, Network* network, Software* software, guint32 ip, GString* hostname, guint32 bwDownKiBps, guint32 bwUpKiBps, guint64 cpuBps);
void node_free(gpointer data);

void node_lock(Node* node);
void node_unlock(Node* node);

void node_startApplication(Node* node);
void node_stopApplication(gpointer key, gpointer value, gpointer user_data);

void node_pushMail(Node* node, Event* event);
Event* node_popMail(Node* node);
void node_pushTask(Node* node, Event* event);
Event* node_popTask(Node* node);
guint node_getNumTasks(Node* node);

gint node_compare(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean node_isEqual(Node* a, Node* b);
CPU* node_getCPU(Node* node);
Network* node_getNetwork(Node* node);
gchar* node_getName(Node* node);
in_addr_t node_getDefaultIP(Node* node);
Application* node_getApplication(Node* node);

gint node_createDescriptor(Node* node, enum DescriptorType type);
gint node_closeDescriptor(Node* node, gint handle);
Descriptor* node_lookupDescriptor(Node* node, gint handle);
NetworkInterface* node_lookupInterface(Node* node, in_addr_t handle);

gint node_epollControl(Node* node, gint epollDescriptor, gint operation,
		gint fileDescriptor, struct epoll_event* event);
gint node_epollGetEvents(Node* node, gint handle, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents);

gint node_bindToInterface(Node* node, gint handle, in_addr_t bindAddress, in_port_t bindPort);
gint node_connectToPeer(Node* node, gint handle, in_addr_t peerAddress, in_port_t peerPort, sa_family_t family);
gint node_listenForPeer(Node* node, gint handle, gint backlog);
gint node_acceptNewPeer(Node* node, gint handle, in_addr_t* ip, in_port_t* port);
gint node_sendUserData(Node* node, gint handle, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_addr_t port, gsize* bytesCopied);
gint node_receiveUserData(Node* node, gint handle, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port, gsize* bytesCopied);
gint node_getPeerName(Node* node, gint handle, in_addr_t* ip, in_port_t* port);
gint node_getSocketName(Node* node, gint handle, in_addr_t* ip, in_port_t* port);

#endif /* SHD_NODE_H_ */
