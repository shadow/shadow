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
	MAGIC_ASSERT(tcp);
}

void tcp_enterServerMode(TCP* tcp, gint backlog) {
	MAGIC_ASSERT(tcp);

	/* we are a server ready to listen, build our server state */
	//TODO
//	/* build the tcp server that will listen at our server port */
//	vtcp_server_tp server = vtcp_server_create(net, sock, backlog);
//	vsocket_mgr_add_server(net, server);
//
//	/* we are now listening for connections */
//	vsocket_transition(sock, VTCP_LISTEN);
}

gint tcp_acceptServerPeer(TCP* tcp, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(tcp);

//	/* make sure we are listening and bound to a addr and port */
//	if(tcp->state != TCPS_LISTEN || !socket_isBound(&(tcp->super))) {
//		return EINVAL;
//	}
//
//	/* get our tcp server */
//	vtcp_server_tp server = vsocket_mgr_get_server(net, sock);
//	if(server == NULL){
//		errno = EINVAL;
//		return VSOCKET_ERROR;
//	}
//
//	/* if there are no pending connection ready to accept, dont block waiting */
//	vtcp_server_child_tp pending_child = vtcp_server_remove_child_pending(server);
//	vsocket_tp pending_sock = pending_child == NULL ? NULL : pending_child->sock;
//	if(pending_sock == NULL) {
//		errno = EWOULDBLOCK;
//		return VSOCKET_ERROR;
//	}
//
//	/* we have a connection and socket ready, it will now be accepted
//	 * make sure socket is still good */
//	if(pending_sock->vt->vtcp == NULL || pending_sock->curr_state != VTCP_ESTABLISHED){
//		/* close stale socket whose connection was reset before accepted */
//		if(pending_sock->vt->vtcp != NULL && pending_sock->vt->vtcp->connection_was_reset) {
//			vsocket_close(net, pending_sock->sock_desc);
//		}
//		errno = ECONNABORTED;
//		return VSOCKET_ERROR;
//	}
//
//	if(pending_sock->vt->vtcp->remote_peer == NULL) {
//		error("no remote peer on pending connection");
//		errno = VSOCKET_ERROR;
//		return VSOCKET_ERROR;
//	}
//
//	vtcp_server_add_child_accepted(server, pending_child);
//
//	/* update child status */
//	vepoll_mark_active(pending_sock->vep);
//	vepoll_mark_available(pending_sock->vep, VEPOLL_WRITE);
//
//	/* update server status */
//	if(g_queue_get_length(server->pending_queue) > 0) {
//		vepoll_mark_available(sock->vep, VEPOLL_READ);
//	} else {
//		vepoll_mark_unavailable(sock->vep, VEPOLL_READ);
//	}

	return 0;
}

void tcp_free(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	MAGIC_CLEAR(tcp);
	g_free(tcp);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable tcp_functions = {
	(SocketIsFamilySupportedFunc) tcp_isFamilySupported,
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
