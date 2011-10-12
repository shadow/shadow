/**
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

Node* node_new(GQuark id, Network* network, Software* software, GString* hostname, guint32 bwDownKiBps, guint32 bwUpKiBps, guint64 cpuBps) {
	Node* node = g_new0(Node, 1);
	MAGIC_INIT(node);

	node->id = id;
	node->event_mailbox = g_async_queue_new_full(event_free);
	node->event_priority_queue = g_queue_new();
	node->network = network;

	node->node_lock = g_mutex_new();

//	node->application = application_new();
//  FIXME create this!
//	node->vsocket_mgr = vsocket_mgr_create(context_provider, ipAddress, KBps_down, KBps_up, cpu_speed_Bps);

	return node;
}

void node_free(gpointer data) {
	Node* node = data;
	MAGIC_ASSERT(node);

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

	g_async_queue_push_sorted(node->event_mailbox, event, event_compare, NULL);
}

Event* node_popMail(Node* node) {
	MAGIC_ASSERT(node);
	return g_async_queue_try_pop(node->event_mailbox);
}

void node_pushTask(Node* node, Event* event) {
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(event);

	g_queue_insert_sorted(node->event_priority_queue, event, event_compare, NULL);
}

Event* node_popTask(Node* node) {
	MAGIC_ASSERT(node);
	return g_queue_pop_head(node->event_priority_queue);
}

guint node_getNumTasks(Node* node) {
	MAGIC_ASSERT(node);
	return g_queue_get_length(node->event_priority_queue);
}

gchar* node_getApplicationArguments(Node* node) {
	MAGIC_ASSERT(node);
	return node->application->software->arguments->str;
}

gint node_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Node* na = a;
	const Node* nb = b;
	MAGIC_ASSERT(na);
	MAGIC_ASSERT(nb);
	return na->id > nb->id ? +1 : na->id == nb->id ? 0 : -1;
}

gboolean node_equal(Node* a, Node* b) {
	if(a == NULL && b == NULL) {
		return TRUE;
	} else if(a == NULL || b == NULL) {
		return FALSE;
	} else {
		return node_compare(a, b, NULL) == 0;
	}
}
