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

struct _InterfaceSentEvent {
	Event super;
	NetworkInterface* interface;
	MAGIC_DECLARE;
};

EventFunctionTable interfacesent_functions = {
	(EventRunFunc) interfacesent_run,
	(EventFreeFunc) interfacesent_free,
	MAGIC_VALUE
};

InterfaceSentEvent* interfacesent_new(NetworkInterface* interface) {
	InterfaceSentEvent* event = g_new0(InterfaceSentEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &interfacesent_functions);

	event->interface = interface;

	return event;
}

void interfacesent_run(InterfaceSentEvent* event, Node* node) {
	MAGIC_ASSERT(event);

	debug("event started");

	networkinterface_sent(event->interface);

	debug("event finished");
}

void interfacesent_free(InterfaceSentEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
