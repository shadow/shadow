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

EventFunctionTable tcpclosetimerexpired_functions = {
	(EventRunFunc) tcpclosetimerexpired_run,
	(EventFreeFunc) tcpclosetimerexpired_free,
	MAGIC_VALUE
};

TCPCloseTimerExpiredEvent* tcpclosetimerexpired_new(GQuark callerID,
		GQuark sourceID, in_port_t sourcePort, GQuark destinationID,
		in_port_t destinationPort, guint32 receiveEnd) {
	TCPCloseTimerExpiredEvent* event = g_new0(TCPCloseTimerExpiredEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &tcpclosetimerexpired_functions);
	event->callerID = callerID;
	event->sourceID = sourceID;
	event->sourcePort = sourcePort;
	event->destinationID = destinationID;
	event->destinationPort = destinationPort;
	event->receiveEnd = receiveEnd;

	return event;
}

void tcpclosetimerexpired_run(TCPCloseTimerExpiredEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	vsocket_mgr_tp vs_mgr = node->vsocket_mgr;
	vsocket_tp sock = vsocket_mgr_find_socket(vs_mgr, SOCK_STREAM,
			(in_addr_t) event->sourceID, event->sourcePort, event->destinationPort);

	if(sock != NULL && sock->vt != NULL && sock->vt->vtcp != NULL) {
		if(sock->curr_state == VTCP_CLOSING) {
			/* we initiated a close, other end got all data and scheduled this event */
			vsocket_transition(sock, VTCP_CLOSED);
			vsocket_mgr_destroy_and_remove_socket(vs_mgr, sock);
		} else if(sock->curr_state == VTCP_LISTEN) {
			/* some other end is closing, we are listening so we do not care.
			 * probably this means that the child that this was actually meant for
			 * was already deleted, so vsocket_mgr_find_socket returned the
			 * parent listener instead. just ignore. */
			return;
		} else {
			/* other end is initiating a close */
			vsocket_transition(sock, VTCP_CLOSE_WAIT);
			sock->vt->vtcp->rcv_end = event->receiveEnd;

			/* we should close after client reads all remaining data */
			sock->do_delete = 1;

			/* other end will not accept any more data */
			vbuffer_clear_send(sock->vt->vb);
			vbuffer_clear_tcp_retransmit(sock->vt->vb, 0, 0);

			/* and we are done, but have to wait to get everything from vs_mgrwork
			 * and then for client to read EOF */
			if(event->receiveEnd <= sock->vt->vtcp->rcv_nxt) {
				/* we already got everything they will send, tell them they should close */
				network_scheduleClose(node->id, event->destinationID, event->destinationPort,
						event->sourceID, event->sourcePort, 0);

				/* tell vepoll that we are ready to read EOF */
				vepoll_mark_available(sock->vep, VEPOLL_READ);
			}
		}
	}
}

void tcpclosetimerexpired_free(TCPCloseTimerExpiredEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
