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

EventVTable socketactivated_vtable = {
	(EventRunFunc) socketactivated_run,
	(EventFreeFunc) socketactivated_free,
	MAGIC_VALUE
};

SocketActivatedEvent* socketactivated_new(guint16 socketDescriptor) {
	SocketActivatedEvent* event = g_new0(SocketActivatedEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &socketactivated_vtable);
	event->socketDescriptor = socketDescriptor;

	return event;
}

void socketactivated_run(SocketActivatedEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	vsocket_mgr_tp vs_mgr = node->vsocket_mgr;
	g_assert(vs_mgr);

	/* check for a pipe */
	vepoll_tp pipe_poll = vpipe_get_poll(vs_mgr->vpipe_mgr, event->socketDescriptor);
	if(pipe_poll != NULL) {
		vepoll_execute_notification(pipe_poll);
	} else {
		/* o/w a socket */
		vsocket_tp sock = vsocket_mgr_get_socket(vs_mgr, event->socketDescriptor);
		if(sock != NULL && sock->vep != NULL) {
			vepoll_execute_notification(sock->vep);
		} else {
			info("socket %u no longer exists, skipping notification.", event->socketDescriptor);
		}
	}
}

void socketactivated_free(SocketActivatedEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
