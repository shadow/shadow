/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "shadow.h"

struct _TCPCloseTimerExpiredEvent {
	Event super;
	TCP* tcp;
	MAGIC_DECLARE;
};

EventFunctionTable tcpclosetimerexpired_functions = {
	(EventRunFunc) tcpclosetimerexpired_run,
	(EventFreeFunc) tcpclosetimerexpired_free,
	MAGIC_VALUE
};

TCPCloseTimerExpiredEvent* tcpclosetimerexpired_new(TCP* tcp) {
	TCPCloseTimerExpiredEvent* event = g_new0(TCPCloseTimerExpiredEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &tcpclosetimerexpired_functions);
	event->tcp = tcp;
	descriptor_ref(tcp);

	return event;
}

void tcpclosetimerexpired_run(TCPCloseTimerExpiredEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	tcp_closeTimerExpired(event->tcp);
}

void tcpclosetimerexpired_free(TCPCloseTimerExpiredEvent* event) {
	MAGIC_ASSERT(event);

	descriptor_unref(event->tcp);

	MAGIC_CLEAR(event);
	g_free(event);
}
