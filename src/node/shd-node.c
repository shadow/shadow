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

#include "shadow.h"

Node* node_new(GQuark id, Network* network, Software* software, guint32 ip, GString* hostname, guint32 bwDownKiBps, guint32 bwUpKiBps, guint64 cpuBps) {
	Node* node = g_new0(Node, 1);
	MAGIC_INIT(node);

	node->id = id;

	/* communication with other nodes */
	node->event_mailbox = g_async_queue_new_full(shadowevent_free);
	node->event_priority_queue = g_queue_new();
	node->network = network;

	node->node_lock = g_mutex_new();

	/* create virtual interfaces */
	node->interfaces = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, networkinterface_free);
	NetworkInterface* ethernet = networkinterface_new(id, hostname->str, bwDownKiBps, bwUpKiBps);
	g_hash_table_replace(node->interfaces, &(ethernet->address->ip), ethernet);
	NetworkInterface* loopback = networkinterface_new((GQuark)htonl(INADDR_LOOPBACK), "loopback", G_MAXUINT32, G_MAXUINT32);
	g_hash_table_replace(node->interfaces, &(loopback->address->ip), loopback);
	node->defaultInterface = ethernet;

	node->application = application_new(software);

	node->descriptors = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, descriptor_unref);
	node->descriptorHandleCounter = MIN_DESCRIPTOR;
	node->randomPortCounter = MIN_RANDOM_PORT;

	// TODO refactor all the socket/event code
	node->vsocket_mgr = vsocket_mgr_create((in_addr_t) id, bwDownKiBps, bwUpKiBps, cpuBps);

	info("Created Node '%s', ip %s, %u bwUpKiBps, %u bwDownKiBps, %lu cpuBps",
			g_quark_to_string(node->id), address_toHostIPString(node->defaultInterface->address),
			bwUpKiBps, bwDownKiBps, cpuBps);

	return node;
}

void node_free(gpointer data) {
	Node* node = data;
	MAGIC_ASSERT(node);

	/* this was hopefully freed in node_stopApplication */
	if(node->application) {
		node_stopApplication(NULL, node, NULL);
	}

	vsocket_mgr_destroy(node->vsocket_mgr);

	g_hash_table_destroy(node->interfaces);

	g_async_queue_unref(node->event_mailbox);
	g_queue_free(node->event_priority_queue);

	g_mutex_free(node->node_lock);

	MAGIC_CLEAR(node);
	g_free(node);
}

void node_lock(Node* node) {
	MAGIC_ASSERT(node);
	g_mutex_lock(node->node_lock);
}

void node_unlock(Node* node) {
	MAGIC_ASSERT(node);
	g_mutex_unlock(node->node_lock);
}

void node_pushMail(Node* node, Event* event) {
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(event);

	g_async_queue_push_sorted(node->event_mailbox, event, shadowevent_compare, NULL);
}

Event* node_popMail(Node* node) {
	MAGIC_ASSERT(node);
	return g_async_queue_try_pop(node->event_mailbox);
}

void node_pushTask(Node* node, Event* event) {
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(event);

	g_queue_insert_sorted(node->event_priority_queue, event, shadowevent_compare, NULL);
}

Event* node_popTask(Node* node) {
	MAGIC_ASSERT(node);
	return g_queue_pop_head(node->event_priority_queue);
}

guint node_getNumTasks(Node* node) {
	MAGIC_ASSERT(node);
	return g_queue_get_length(node->event_priority_queue);
}

void node_startApplication(Node* node) {
	MAGIC_ASSERT(node);
	application_boot(node->application);
}

void node_stopApplication(gpointer key, gpointer value, gpointer user_data) {
	Node* node = value;
	MAGIC_ASSERT(node);

	Worker* worker = worker_getPrivate();
	worker->cached_node = node;

	application_free(node->application);
	node->application = NULL;

	worker->cached_node = NULL;
}

gint node_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Node* na = a;
	const Node* nb = b;
	MAGIC_ASSERT(na);
	MAGIC_ASSERT(nb);
	return na->id > nb->id ? +1 : na->id == nb->id ? 0 : -1;
}

gboolean node_isEqual(Node* a, Node* b) {
	if(a == NULL && b == NULL) {
		return TRUE;
	} else if(a == NULL || b == NULL) {
		return FALSE;
	} else {
		return node_compare(a, b, NULL) == 0;
	}
}

guint32 node_getBandwidthUp(Node* node) {
	MAGIC_ASSERT(node);
	return node->vsocket_mgr->vt_mgr->KBps_up;
}

guint32 node_getBandwidthDown(Node* node) {
	MAGIC_ASSERT(node);
	return node->vsocket_mgr->vt_mgr->KBps_down;
}

static gint _node_monitorDescriptor(Node* node, Descriptor* descriptor) {
	MAGIC_ASSERT(node);

	gint* handle = descriptor_getHandleReference(descriptor);
	/* @todo add check if something exists at this key */
	g_hash_table_replace(node->descriptors, handle, descriptor);

	return *handle;
}

gint node_createDescriptor(Node* node, enum DescriptorType type) {
	MAGIC_ASSERT(node);

	/* get a unique descriptor that can be "closed" later */
	Descriptor* descriptor;

	switch(type) {
		case DT_EPOLL: {
			descriptor = (Descriptor*) epoll_new((node->descriptorHandleCounter)++);
			break;
		}

		case DT_TCPSOCKET: {
			descriptor = (Descriptor*) tcp_new((node->descriptorHandleCounter)++);
			break;
		}

		case DT_UDPSOCKET: {
			descriptor = (Descriptor*) udp_new((node->descriptorHandleCounter)++);
			break;
		}

		default: {
			warning("unknown descriptor type: %i", (int)type);
			return EINVAL;
		}
	}

	return _node_monitorDescriptor(node, descriptor);
}

Descriptor* node_lookupDescriptor(Node* node, gint handle) {
	MAGIC_ASSERT(node);
	return g_hash_table_lookup(node->descriptors, (gconstpointer) &handle);
}

gint node_epollControl(Node* node, gint epollDescriptor, gint operation,
		gint fileDescriptor, struct epoll_event* event) {
	MAGIC_ASSERT(node);
	g_assert(node->descriptors);

	/* EBADF  epfd is not a valid file descriptor. */
	Descriptor* descriptor = g_hash_table_lookup(node->descriptors, &epollDescriptor);
	if(descriptor == NULL) {
		return EBADF;
	}

	/* EINVAL epfd is not an epoll file descriptor */
	if(descriptor_getType(descriptor) != DT_EPOLL) {
		return EINVAL;
	}

	/* now we know its an epoll */
	Epoll* epoll = (Epoll*) descriptor;

	/* EBADF  fd is not a valid file descriptor. */
	descriptor = g_hash_table_lookup(node->descriptors, &fileDescriptor);
	if(descriptor == NULL) {
		return EBADF;
	}

	return epoll_control(epoll, operation, descriptor, event);

}

gint node_epollGetEvents(Node* node, gint handle,
		struct epoll_event* eventArray, gint eventArrayLength, gint* nEvents) {
	MAGIC_ASSERT(node);
	g_assert(node->descriptors);

	/* EBADF  epfd is not a valid file descriptor. */
	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		return EBADF;
	}

	/* EINVAL epfd is not an epoll file descriptor */
	if(descriptor_getType(descriptor) != DT_EPOLL) {
		return EINVAL;
	}

	Epoll* epoll = (Epoll*) descriptor;
	return epoll_getEvents(epoll, eventArray, eventArrayLength, nEvents);
}

static gboolean _node_doesInterfaceExist(Node* node, in_addr_t interfaceIP) {
	MAGIC_ASSERT(node);

	if(interfaceIP == htonl(INADDR_ANY) && node->defaultInterface) {
		return TRUE;
	}

	NetworkInterface* interface = g_hash_table_lookup(node->interfaces, &interfaceIP);
	if(interface) {
		return TRUE;
	}

	return FALSE;
}

static gboolean _node_isInterfaceAvailable(Node* node, in_addr_t interfaceIP,
		enum DescriptorType type, in_port_t port) {
	MAGIC_ASSERT(node);

	gboolean isAvailable = FALSE;

	if(interfaceIP == htonl(INADDR_ANY)) {
		/* need to check that all interfaces are free */
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, node->interfaces);

		while(g_hash_table_iter_next(&iter, &key, &value)) {
			NetworkInterface* interface = value;
			isAvailable = networkinterface_isAssociated(interface, type, port);

			/* as soon as one is taken, break out to return FALSE */
			if(!isAvailable) {
				break;
			}
		}
	} else {
		NetworkInterface* interface = g_hash_table_lookup(node->interfaces, &interfaceIP);
		isAvailable = networkinterface_isAssociated(interface, type, port);
	}

	return isAvailable;
}


static in_port_t _node_getRandomFreePort(Node* node, in_addr_t interfaceIP,
		enum DescriptorType type) {
	MAGIC_ASSERT(node);

	in_port_t randomPort = 0;
	gboolean available = FALSE;

	while(!available) {
		randomPort = (node->randomPortCounter)++;
		g_assert(randomPort >= MIN_RANDOM_PORT);
		available = _node_isInterfaceAvailable(node, interfaceIP, type, randomPort);
	}

	return randomPort;
}

static void _node_associateInterface(Node* node, in_addr_t interfaceIP, Socket* socket) {
	MAGIC_ASSERT(node);

	if(interfaceIP == htonl(INADDR_ANY)) {
		/* need to associate all interfaces */
		GHashTableIter iter;
		gpointer key, value;
		g_hash_table_iter_init(&iter, node->interfaces);

		while(g_hash_table_iter_next(&iter, &key, &value)) {
			NetworkInterface* interface = value;
			MAGIC_ASSERT(interface);
			networkinterface_associate(interface, socket);
		}
	} else {
		NetworkInterface* interface = g_hash_table_lookup(node->interfaces, &interfaceIP);
		networkinterface_associate(interface, socket);
	}
}

gint node_bindToInterface(Node* node, gint handle, in_addr_t bindAddress, in_port_t bindPort) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	enum DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return ENOTSOCK;
	}

	/* make sure we have an interface at that address */
	if(!_node_doesInterfaceExist(node, bindAddress)) {
		return EADDRNOTAVAIL;
	}

	Socket* socket = (Socket*) descriptor;

	/* make sure socket is not bound */
	if(socket_isBound(socket)) {
		warning("socket already bound to requested address");
		return EINVAL;
	}

	/* make sure we have a proper port */
	if(bindPort == 0) {
		/* we know it will be available */
		bindPort = _node_getRandomFreePort(node, bindAddress, type);
	} else {
		/* make sure their port is available at that address for this protocol. */
		if(!_node_isInterfaceAvailable(node, bindAddress, type, bindPort)) {
			return EADDRINUSE;
		}
	}

	/* bind socket and set association on the interface */
	socket_bindToInterface(socket, bindAddress, bindPort);
	_node_associateInterface(node, bindAddress, socket);

	return 0;
}

gint node_connectToPeer(Node* node, gint handle, in_addr_t peerAddress,
		in_port_t peerPort, sa_family_t family) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	enum DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return ENOTSOCK;
	}

	Socket* socket = (Socket*) descriptor;

	if(!socket_isFamilySupported(socket, family)) {
		return EAFNOSUPPORT;
	}

	if(type == DT_TCPSOCKET) {
		gint error = tcp_getConnectError((TCP*)socket);
		if(error) {
			return error;
		}
	}

	if(!socket_isBound(socket)) {
		/* do an implicit bind to a random port.
		 * use default interface unless the remote peer is on loopback */
		in_addr_t loIP = htonl(INADDR_LOOPBACK);
		in_addr_t defaultIP = networkinterface_getIPAddress(node->defaultInterface);

		in_addr_t bindAddress = loIP == peerAddress ? loIP : defaultIP;
		in_port_t bindPort = _node_getRandomFreePort(node, bindAddress, type);

		socket_bindToInterface(socket, bindAddress, bindPort);
		_node_associateInterface(node, bindAddress, socket);
	}

	return socket_connectToPeer(socket, peerAddress, peerPort, family);
}

gint node_listenForPeer(Node* node, gint handle, gint backlog) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	enum DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return EOPNOTSUPP;
	}

	Socket* socket = (Socket*) descriptor;

	if(!socket_isBound(socket)) {
		/* implicit bind */
		in_addr_t bindAddress = htonl(INADDR_ANY);
		in_port_t bindPort = _node_getRandomFreePort(node, bindAddress, type);

		socket_bindToInterface(socket, bindAddress, bindPort);
		_node_associateInterface(node, bindAddress, socket);
	}

	tcp_enterServerMode((TCP*)socket, backlog);
	return 0;
}

gint node_acceptNewPeer(Node* node, gint handle, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	enum DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET) {
		return EOPNOTSUPP;
	}

	return tcp_acceptServerPeer((TCP*)descriptor, ip, port);
}

gint node_getPeerName(Node* node, gint handle, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	enum DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET) {
		return ENOTCONN;
	}

	return socket_getPeerName((Socket*)descriptor, ip, port);
}

gint node_getSocketName(Node* node, gint handle, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	enum DescriptorType type = descriptor_getType(descriptor);
	if(type != DT_TCPSOCKET && type != DT_UDPSOCKET) {
		warning("wrong type for descriptor handle '%i'", handle);
		return ENOTSOCK;
	}

	return socket_getSocketName((Socket*)descriptor, ip, port);
}


gssize node_sendToPeer(Node* node, gint handle, gconstpointer buffer, gsize nBytes,
		in_addr_t ip, in_addr_t port) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	return -1;
}

gssize node_receiveFromPeer(Node* node, gint handle, gpointer buffer, gsize nBytes,
		in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	return -1;
}

gint node_closeDescriptor(Node* node, gint handle) {
	MAGIC_ASSERT(node);

	Descriptor* descriptor = node_lookupDescriptor(node, handle);
	if(descriptor == NULL) {
		warning("descriptor handle '%i' not found", handle);
		return EBADF;
	}

	return -1;
}
