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
	node->address = address_new(id, (const gchar*) hostname->str);
	node->event_mailbox = g_async_queue_new_full(shadowevent_free);
	node->event_priority_queue = g_queue_new();
	node->network = network;

	node->node_lock = g_mutex_new();

	node->application = application_new(software);

	node->descriptors = g_tree_new_full(descriptor_compare, NULL, NULL, descriptor_free);
	node->descriptorHandleCounter = VNETWORK_MIN_SD;

	// TODO refactor all the socket/event code
	node->vsocket_mgr = vsocket_mgr_create((in_addr_t) id, bwDownKiBps, bwUpKiBps, cpuBps);

	info("Created Node '%s', ip %s, %u bwUpKiBps, %u bwDownKiBps, %lu cpuBps",
			g_quark_to_string(node->id), address_toHostIPString(node->address),
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

	address_free(node->address);
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
	g_tree_replace(node->descriptors, handle, descriptor);

	return *handle;
}

gint node_epollNew(Node* node) {
	MAGIC_ASSERT(node);

	/* get a unique descriptor that can be "closed" later */
	EpollDescriptor* epoll = epoll_new((node->descriptorHandleCounter)++);
	return _node_monitorDescriptor(node, (Descriptor*) epoll);
}

gint node_epollControl(Node* node, gint epollDescriptor, gint operation,
		gint fileDescriptor, struct epoll_event* event) {
	MAGIC_ASSERT(node);
	g_assert(node->descriptors);

	/* EBADF  epfd is not a valid file descriptor. */
	Descriptor* descriptor = g_tree_lookup(node->descriptors, &epollDescriptor);
	if(descriptor == NULL) {
		return EBADF;
	}

	/* EINVAL epfd is not an epoll file descriptor */
	if(descriptor_getType(descriptor) != DT_EPOLL) {
		return EINVAL;
	}

	/* now we know its an epoll */
	EpollDescriptor* epoll = (EpollDescriptor*) descriptor;

	/* EBADF  fd is not a valid file descriptor. */
	descriptor = g_tree_lookup(node->descriptors, &fileDescriptor);
	if(descriptor == NULL) {
		return EBADF;
	}

	return epoll_control(epoll, operation, descriptor, event);

}

gint node_epollGetEvents(Node* node, gint epollDescriptor,
		struct epoll_event* eventArray, gint eventArrayLength, gint* nEvents) {
	MAGIC_ASSERT(node);
	g_assert(node->descriptors);

	/* EBADF  epfd is not a valid file descriptor. */
	Descriptor* descriptor = g_tree_lookup(node->descriptors, &epollDescriptor);
	if(descriptor == NULL) {
		return EBADF;
	}

	/* EINVAL epfd is not an epoll file descriptor */
	if(descriptor_getType(descriptor) != DT_EPOLL) {
		return EINVAL;
	}

	EpollDescriptor* epoll = (EpollDescriptor*) descriptor;
	return epoll_getEvents(epoll, eventArray, eventArrayLength, nEvents);
}
