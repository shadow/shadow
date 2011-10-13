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

EventVTable tcpclosetimerexpired_vtable = {
	(EventRunFunc) tcpclosetimerexpired_run,
	(EventFreeFunc) tcpclosetimerexpired_free,
	MAGIC_VALUE
};

TCPCloseTimerExpiredEvent* tcpclosetimerexpired_new() {
	TCPCloseTimerExpiredEvent* event = g_new0(TCPCloseTimerExpiredEvent, 1);
	MAGIC_INIT(event);

	event_init(&(event->super), &tcpclosetimerexpired_vtable);


	return event;
}

void tcpclosetimerexpired_run(TCPCloseTimerExpiredEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

}

void tcpclosetimerexpired_free(TCPCloseTimerExpiredEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
