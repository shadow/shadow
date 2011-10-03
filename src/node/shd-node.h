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

#ifndef SHD_NODE_H_
#define SHD_NODE_H_

typedef struct _Node Node;

struct _Node {
	/* asynchronous event priority queue. other nodes may push to this queue. */
	GAsyncQueue* event_mailbox;

	/* general node lock. nothing that belongs to the node should be touched
	 * unless holding this lock.
	 */
	GMutex* node_lock;

	/* a simple priority queue holding events currently being executed.
	 * events are place in this queue before handing the node off to a
	 * worker and should not be modified by other nodes. */
	GQueue* event_priority_queue;

	gint node_id;

	MAGIC_DECLARE;
};

Node* node_new();
void node_free(Node* node);

void node_lock(Node* node);
void node_unlock(Node* node);

void node_mail_push(Node* node, Event* event);
Event* node_mail_pop(Node* node);
void node_task_push(Node* node, Event* event);
Event* node_task_pop(Node* node);

gint node_compare(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean node_equal(Node* a, Node* b);

#endif /* SHD_NODE_H_ */
