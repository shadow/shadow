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

	node->event_mailbox = g_async_queue_new_full(event_free);
	node->event_priority_queue = g_queue_new();

	return node;
}

void node_free(Node* node) {
	g_assert(node);
	g_async_queue_unref(node->event_mailbox);
	g_queue_free(node->event_priority_queue);
	g_free(node);
}
