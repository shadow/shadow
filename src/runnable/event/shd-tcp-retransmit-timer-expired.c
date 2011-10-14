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

EventVTable tcpretransmittimerexpired_vtable = {
	(EventRunFunc) tcpretransmittimerexpired_run,
	(EventFreeFunc) tcpretransmittimerexpired_free,
	MAGIC_VALUE
};

TCPRetransmitTimerExpiredEvent* tcpretransmittimerexpired_new(GQuark callerID,
		GQuark sourceID, in_port_t sourcePort, GQuark destinationID,
		in_port_t destinationPort, guint32 retransmitKey) {
	TCPRetransmitTimerExpiredEvent* event = g_new0(TCPRetransmitTimerExpiredEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &tcpretransmittimerexpired_vtable);
	event->callerID = callerID;
	event->sourceID = sourceID;
	event->sourcePort = sourcePort;
	event->destinationID = destinationID;
	event->destinationPort = destinationPort;
	event->retransmitKey = retransmitKey;

	return event;
}

void tcpretransmittimerexpired_run(TCPRetransmitTimerExpiredEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	debug("event fired\n");

	vsocket_mgr_tp vs_mgr = node->vsocket_mgr;
	if(vs_mgr == NULL) {
		return;
	}

	debug("%s:%u requesting retransmission of %u from %s:%u",
			NTOA(event->destinationID), ntohs(event->destinationPort),
			event->retransmitKey, vs_mgr->addr_string, ntohs(event->sourcePort));

	vsocket_tp sock = vsocket_mgr_find_socket(vs_mgr, SOCK_STREAM,
			(in_addr_t)event->destinationID, event->destinationPort, event->sourcePort);

	if(sock == NULL || sock->vt == NULL) {
		return;
	}

	if(sock->vt->vtcp != NULL && sock->vt->vtcp->remote_peer == NULL) {
		info("%s:%u has no connected child socket. was it closed?",
				NTOA(vs_mgr->addr), ntohs(event->sourcePort));
		return;
	}

	vtcp_retransmit(sock->vt->vtcp, event->retransmitKey);
}

void tcpretransmittimerexpired_free(TCPRetransmitTimerExpiredEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
