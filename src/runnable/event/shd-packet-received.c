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

EventVTable packetreceived_vtable = {
	(EventRunFunc) packetreceived_run,
	(EventFreeFunc) packetreceived_free,
	MAGIC_VALUE
};

PacketReceivedEvent* packetreceived_new() {
	PacketReceivedEvent* event = g_new0(PacketReceivedEvent, 1);
	MAGIC_INIT(event);

	event_init(&(event->super), &packetreceived_vtable);


	return event;
}

void packetreceived_run(PacketReceivedEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

}

void packetreceived_free(PacketReceivedEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
