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

EventFunctionTable tcpdacktimerexpired_functions = {
	(EventRunFunc) tcpdacktimerexpired_run,
	(EventFreeFunc) tcpdacktimerexpired_free,
	MAGIC_VALUE
};

TCPDAckTimerExpiredEvent* tcpdacktimerexpired_new(guint16 socketDescriptor) {
	TCPDAckTimerExpiredEvent* event = g_new0(TCPDAckTimerExpiredEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &tcpdacktimerexpired_functions);
	event->socketDescriptor = socketDescriptor;

	return event;
}

void tcpdacktimerexpired_run(TCPDAckTimerExpiredEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	/* a delayed ack timer expired, send ack if needed */
	vsocket_tp sock = vsocket_mgr_get_socket(node->vsocket_mgr, event->socketDescriptor);
	if(sock != NULL && sock->vt != NULL) {
		vtcp_checkdack(sock->vt->vtcp);
	}

}

void tcpdacktimerexpired_free(TCPDAckTimerExpiredEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
