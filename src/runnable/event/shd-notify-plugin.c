/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"
#include "shd-event-internal.h"

struct _NotifyPluginEvent {
	Event super;
	gint epollHandle;
	MAGIC_DECLARE;
};

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

void notifyplugin_run(NotifyPluginEvent* event, Host* node) {
	MAGIC_ASSERT(event);

	debug("event started");

	/* check in with epoll to make sure we should carry out the notification */
	Epoll* epoll = (Epoll*) host_lookupDescriptor(node, event->epollHandle);
	if(epoll) {
        epoll_tryNotify(epoll);
	}

	debug("event finished");
}

void notifyplugin_free(NotifyPluginEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
