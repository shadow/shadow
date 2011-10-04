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

Node* node_new() {
	Node* node = g_new(Node, 1);
	MAGIC_INIT(node);

	node->event_mailbox = g_async_queue_new_full(event_free);
	node->event_priority_queue = g_queue_new();

	return node;
}

void node_free(gpointer data) {
	Node* node = data;
	MAGIC_ASSERT(node);

	g_async_queue_unref(node->event_mailbox);
	g_queue_free(node->event_priority_queue);

	MAGIC_CLEAR(node);
	g_free(node);
}

void node_lock(Node* node) {
	MAGIC_ASSERT(node);
}

void node_unlock(Node* node) {
	MAGIC_ASSERT(node);
}

void node_pushMail(Node* node, NodeEvent* event) {
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(event);

	g_async_queue_push_sorted(node->event_mailbox, event, event_compare, NULL);
}

NodeEvent* node_popMail(Node* node) {
	MAGIC_ASSERT(node);
	return g_async_queue_try_pop(node->event_mailbox);
}

void node_pushTask(Node* node, NodeEvent* event) {
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(event);

	g_queue_insert_sorted(node->event_priority_queue, event, event_compare, NULL);
}

NodeEvent* node_popTask(Node* node) {
	MAGIC_ASSERT(node);
	return g_queue_pop_head(node->event_priority_queue);
}

guint node_getNumTasks(Node* node) {
	MAGIC_ASSERT(node);
	return g_queue_get_length(node->event_priority_queue);
}

gint node_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Node* na = a;
	const Node* nb = b;
	MAGIC_ASSERT(na);
	MAGIC_ASSERT(nb);
	return na->node_id > nb->node_id ? +1 : na->node_id == nb->node_id ? 0 : -1;
}

gboolean node_equal(Node* a, Node* b) {
	return node_compare(a, b, NULL) == 0;
}
