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

enum TCPState {
	TCPS_NONE, TCPS_LISTEN,
	TCPS_SYNSENT, TCPS_SYNRECEIVED, TCPS_ESTABLISHED,
	TCPS_CLOSING, TCPS_CLOSEWAIT, TCPS_CLOSED,
};

enum TCPFlags {
	TCPF_NONE = 0,
};

enum TCPError {
	TCPE_NONE = 0,
	TCPE_CONNECTION_RESET = 1 << 0,
};

struct _TCP {
	Socket super;

	enum TCPState state;
	enum TCPFlags flags;
	enum TCPError error;

	MAGIC_DECLARE;
};

gboolean tcp_isFamilySupported(TCP* tcp, sa_family_t family) {
	MAGIC_ASSERT(tcp);
	return family == AF_INET ? TRUE : FALSE;
}

gint tcp_getConnectError(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	if(tcp->error & TCPE_CONNECTION_RESET) {
		return ECONNREFUSED;
	} else if(tcp->state == TCPS_SYNSENT || tcp->state == TCPS_SYNRECEIVED) {
		return EALREADY;
	} else if(tcp->state != TCPS_NONE) {
		/* @todo: this affects ability to connect. if a socket is closed, can
		 * we start over and connect again? if so, this should change
		 */
		return EISCONN;
	}

	return 0;
}

gint tcp_connectToPeer(TCP* tcp, in_addr_t ip, in_port_t port, sa_family_t family) {
	MAGIC_ASSERT(tcp);

	// TODO
//	/* create the connection state */
//	vtcp_connect(sock->vt->vtcp, saddr->sin_addr.s_addr, saddr->sin_port);
//
//	/* send 1st part of 3-way handshake, closed->syn_sent */
//	rc_vpacket_pod_tp rc_packet = vtcp_create_packet(sock->vt->vtcp, SYN | CON, 0, NULL);
//	guint8 success = vtcp_send_packet(sock->vt->vtcp, rc_packet);
//
//	rc_vpacket_pod_release(rc_packet);
//
//	if(!success) {
//		/* this should never happen, control packets consume no buffer space */
//		error("error sending SYN step 1");
//		vtcp_disconnect(sock->vt->vtcp);
//		errno = EAGAIN;
//		return VSOCKET_ERROR;
//	}
//
//	vsocket_transition(sock, VTCP_SYN_SENT);

	/* we dont block, so return EINPROGRESS while waiting for establishment */
	return EINPROGRESS;
}

void tcp_send(TCP* tcp) {

}

void tcp_free(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	MAGIC_CLEAR(tcp);
	g_free(tcp);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable tcp_functions = {
	(SocketIsFamilySupportedFunc) tcp_isFamilySupported,
	(SocketGetConnectErrorFunc) tcp_getConnectError,
	(SocketConnectToPeerFunc) tcp_connectToPeer,
	(SocketSendFunc) tcp_send,
	(SocketFreeFunc) tcp_free,
	MAGIC_VALUE
};

TCP* tcp_new(gint handle) {
	TCP* tcp = g_new0(TCP, 1);
	MAGIC_INIT(tcp);

	socket_init(&(tcp->super), &tcp_functions, DT_TCPSOCKET, handle);

	return tcp;
}
