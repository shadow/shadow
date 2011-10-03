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

#ifndef SHD_NODEEVENT_H_
#define SHD_NODEEVENT_H_

#include "shadow.h"

typedef struct _NodeEvent NodeEvent;
typedef struct _NodeEventVTable NodeEventVTable;

/* FIXME: forward declaration to avoid circular dependencies... */
typedef struct _Node Node;

/* required functions */
typedef void (*NodeEventExecuteFunc)(NodeEvent* event, Node* node);
typedef void (*NodeEventFreeFunc)(NodeEvent* event, Node* node);

/*
 * Virtual function table for base event, storing pointers to required
 * callable functions.
 */
struct _NodeEventVTable {
	NodeEventExecuteFunc execute;
	NodeEventFreeFunc free;

	MAGIC_DECLARE;
};

/*
 * A basic event connected to a specific node. This extends event, and is meant
 * to be extended by most other events.
 */
struct _NodeEvent {
	Event super;
	NodeEventVTable* vtable;
	Node* node;

	MAGIC_DECLARE;
};

void nodeevent_init(NodeEvent* event, NodeEventVTable* vtable, Node* node);
void nodeevent_execute(NodeEvent* event);
void nodeevent_free(gpointer data);

#endif /* SHD_NODEEVENT_H_ */
