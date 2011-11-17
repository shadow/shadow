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

EventFunctionTable notifyplugin_functions = {
	(EventRunFunc) notifyplugin_run,
	(EventFreeFunc) notifyplugin_free,
	MAGIC_VALUE
};

NotifyPluginEvent* notifyplugin_new(gint epollHandle) {
	NotifyPluginEvent* event = g_new0(NotifyPluginEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &notifyplugin_functions);
	event->epollHandle = epollHandle;

	return event;
}

void notifyplugin_run(NotifyPluginEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	/* check in with epoll to make sure we should carry out the notification */
	Descriptor* epoll = node_lookupDescriptor(node, event->epollHandle);
	if(epoll_isReadyToNotify((Epoll*)epoll)) {
		application_notify(node->application);
	}
}

void notifyplugin_free(NotifyPluginEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
