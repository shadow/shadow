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

EventVTable nodeevent_vtable = {
	(EventExecuteFunc)nodeevent_execute,
	(EventFreeFunc)nodeevent_free,
	MAGIC_VALUE
};

void nodeevent_init(NodeEvent* event, NodeEventVTable* vtable, Node* node) {
	g_assert(event && vtable);
	MAGIC_ASSERT(node);

	event_init(&(event->super), &nodeevent_vtable);

	MAGIC_INIT(event);
	MAGIC_INIT(vtable);

	event->vtable = vtable;
	event->node = node;
}

void nodeevent_execute(NodeEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);
	MAGIC_ASSERT(event->node);

	event->vtable->execute(event, event->node);
}

void nodeevent_free(gpointer data) {
	NodeEvent* event = data;
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(event->vtable);
	MAGIC_ASSERT(event->node);

	MAGIC_CLEAR(event);
	MAGIC_CLEAR(event->vtable);

	event->vtable->free(event, event->node);
}
