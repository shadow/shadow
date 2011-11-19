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

struct _Node {
	/* asynchronous event priority queue. other nodes may push to this queue. */
	GAsyncQueue* event_mailbox;

	/* the network this node belongs to */
	Network* network;

	/* general node lock. nothing that belongs to the node should be touched
	 * unless holding this lock. everything following this falls under the lock.
	 */
	GMutex* node_lock;

	/* a simple priority queue holding events currently being executed.
	 * events are place in this queue before handing the node off to a
	 * worker and should not be modified by other nodes. */
	GQueue* event_priority_queue;

	GQuark id;
	GHashTable* interfaces;
	NetworkInterface* defaultInterface;

	Application* application;
	vsocket_mgr_tp vsocket_mgr;

	/* all file, socket, and epoll descriptors we know about and track */
	GHashTable* descriptors;
	gint descriptorHandleCounter;
	in_port_t randomPortCounter;

	MAGIC_DECLARE;
};

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

guint32 node_getBandwidthUp(Node* node);
guint32 node_getBandwidthDown(Node* node);

gint node_createDescriptor(Node* node, enum DescriptorType type);
Descriptor* node_lookupDescriptor(Node* node, gint handle);
gint node_closeDescriptor(Node* node, gint handle);

gint node_epollControl(Node* node, gint epollDescriptor, gint operation,
		gint fileDescriptor, struct epoll_event* event);
gint node_epollGetEvents(Node* node, gint handle, struct epoll_event* eventArray,
		gint eventArrayLength, gint* nEvents);

gint node_bindToInterface(Node* node, gint handle, in_addr_t bindAddress, in_port_t bindPort);
gint node_connectToPeer(Node* node, gint handle, in_addr_t peerAddress, in_port_t peerPort, sa_family_t family);
gint node_listenForPeer(Node* node, gint handle, gint backlog);
gint node_acceptNewPeer(Node* node, gint handle, in_addr_t* ip, in_port_t* port);
gssize node_sendToPeer(Node* node, gint handle, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_addr_t port);
gssize node_receiveFromPeer(Node* node, gint handle, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port);
gint node_getPeerName(Node* node, gint handle, in_addr_t* ip, in_port_t* port);
gint node_getSocketName(Node* node, gint handle, in_addr_t* ip, in_port_t* port);

#endif /* SHD_NODE_H_ */
