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

struct _InterfaceReceivedEvent {
	Event super;
	NetworkInterface* interface;
	MAGIC_DECLARE;
};

EventFunctionTable interfacereceived_functions = {
	(EventRunFunc) interfacereceived_run,
	(EventFreeFunc) interfacereceived_free,
	MAGIC_VALUE
};

InterfaceReceivedEvent* interfacereceived_new(NetworkInterface* interface) {
	InterfaceReceivedEvent* event = g_new0(InterfaceReceivedEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &interfacereceived_functions);

	event->interface = interface;

	return event;
}

void interfacereceived_run(InterfaceReceivedEvent* event, Node* node) {
	MAGIC_ASSERT(event);

	debug("event started");

	networkinterface_received(event->interface);

	debug("event finished");
}

void interfacereceived_free(InterfaceReceivedEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
