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
	TCPF_DELAYEDACK_SCHEDULED = 1 << 0,
	TCPF_DELAYEDACK_REQUESTED = 1 << 1,
};

enum TCPError {
	TCPE_NONE = 0,
	TCPE_CONNECTION_RESET = 1 << 0,
};

enum TCPChildState {
	TCPCS_NONE, TCPCS_INCOMPLETE, TCPCS_PENDING, TCPCS_ACCEPTED
};

typedef struct _TCPChild TCPChild;
struct _TCPChild {
	enum TCPChildState state;
	TCP* tcp;
	guint key; /* hash(peerIP, peerPort) */
	TCP* parent;
	MAGIC_DECLARE;
};

typedef struct _TCPServer TCPServer;
struct _TCPServer {
	/* all children of this server */
	GHashTable* children;
	/* pending children to accept in order. */
	GQueue *pending;
	/* maximum number of pending connections (capped at SOMAXCONN = 128) */
	gint pendingMaxLength;
	MAGIC_DECLARE;
};

struct _TCP {
	Socket super;

	enum TCPState state;
	enum TCPState stateLast;
	enum TCPFlags flags;
	enum TCPError error;

	/* sequence numbers we track for incoming packets */
	struct {
		/* initial receive sequence number */
		guint32 start;
		/* next packet we expect to receive */
		guint32 next;
		/* how far past next can we receive */
		guint32 window;
		/* used to make sure we get all data when other end closes */
		guint32 end;
	} receive;

	/* sequence numbers we track for outgoing packets */
	struct {
		/* packets we've sent but have yet to be acknowledged */
		guint32 unacked;
		/* next packet we can send */
		guint32 next;
		/* how far past next can we send */
		guint32 window;
		/* the last byte that was sent by the app, possibly not yet sent to the network */
		guint32 end;
		/* send sequence number used for last window update */
		guint32 last1;
		/* send ack number used from last window update */
		guint32 last2;
	} send;

	/* congestion control, sequence numbers used for AIMD and slow start */
	gboolean isSlowStart;
	struct {
		gdouble window;
		guint32 threshold;
		guint32 last;
	} congestion;

	/* TCP throttles data packets if too many are in flight */
	GQueue* throttledDataPackets;

	/* tracks a packet that has currently been only partially read, if any */
	Packet* partialUserDataPacket;
	guint partialOffset;

	/* if I am a server, I parent many multiplexed child sockets */
	TCPServer* server;

	/* if I am a multiplexed child, I have a pointer to my parent */
	TCPChild* child;

	MAGIC_DECLARE;
};

static TCPChild* _tcpchild_new(TCP* tcp, TCP* parent, in_addr_t peerIP, in_port_t peerPort) {
	MAGIC_ASSERT(tcp);
	MAGIC_ASSERT(parent);

	TCPChild* child = g_new0(TCPChild, 1);
	MAGIC_INIT(child);

	/* my parent can find me by my key */
	child->key = utility_ipPortHash(peerIP, peerPort);

	descriptor_ref(tcp);
	child->tcp = tcp;
	descriptor_ref(parent);
	child->parent = parent;

	return child;
}

static void _tcpchild_free(TCPChild* child) {
	MAGIC_ASSERT(child);

	descriptor_unref(child->parent);
	descriptor_unref(child->tcp);

	MAGIC_CLEAR(child);
	g_free(child);
}

static TCPServer* _tcpserver_new(gint backlog) {
	TCPServer* server = g_new0(TCPServer, 1);
	MAGIC_INIT(server);

	server->children = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify) _tcpchild_free);
	server->pending = g_queue_new();
	server->pendingMaxLength = backlog;

	return server;
}

static void _tcpserver_free(TCPServer* server) {
	MAGIC_ASSERT(server);

	/* no need to destroy children in this queue */
	g_queue_free(server->pending);
	/* this will unref all children */
	g_hash_table_destroy(server->children);

	MAGIC_CLEAR(server);
	g_free(server);
}

static void _tcp_setState(TCP* tcp, enum TCPState state) {
	MAGIC_ASSERT(tcp);

	tcp->stateLast = tcp->state;
	tcp->state = state;

	/* some state transitions require us to update the descriptor status */
	switch (state) {
		case TCPS_LISTEN: {
			descriptor_adjustStatus((Descriptor*)tcp, DS_ACTIVE, TRUE);
			break;
		}
		case TCPS_SYNSENT: {
			break;
		}
		case TCPS_SYNRECEIVED: {
			break;
		}
		case TCPS_ESTABLISHED: {
			descriptor_adjustStatus((Descriptor*)tcp, DS_ACTIVE|DS_WRITABLE, TRUE);
			break;
		}
		case TCPS_CLOSING: {
			descriptor_adjustStatus((Descriptor*)tcp, DS_STALE, TRUE);
			break;
		}
		case TCPS_CLOSEWAIT: {
			/* user needs to read a 0 so it knows we closed */
			descriptor_adjustStatus((Descriptor*)tcp, DS_STALE, TRUE);
			break;
		}
		case TCPS_CLOSED: {
			descriptor_adjustStatus((Descriptor*)tcp, DS_ACTIVE, FALSE);
			descriptor_adjustStatus((Descriptor*)tcp, DS_STALE, TRUE);
			break;
		}
		case TCPS_NONE:
		default:
			break;
	}
}

static gboolean _tcp_willThrottlePacket(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);

	PacketTCPHeader header;
	packet_getTCPHeader(packet, &header);

	/* we dont throttle control packets without any payload */
	if(packet_getPayloadLength(packet) > 0 &&
			((tcp->send.unacked + tcp->send.window) <= header.sequence)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

static void _tcp_updateReceiveWindow(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	gsize space = tcp->super.super.outputBufferSize - tcp->super.super.outputBufferLength;
	gsize nPackets = space / (CONFIG_MTU - CONFIG_TCPIP_HEADER_SIZE);

	tcp->receive.window = nPackets;
	g_assert(tcp->receive.window > 0);
}

static gboolean _tcp_updateSendWindow(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	guint32 old = tcp->send.window;

	/* send window is minimum of congestion window and advertised/old send window */
	tcp->send.window = MIN(((guint32)tcp->congestion.window), tcp->congestion.last);
	if(tcp->send.window < 1) {
		tcp->send.window = 1;
	}

	/* return true if the window opened */
	if(tcp->send.window > old) {
		return TRUE;
	} else {
		return FALSE;
	}
}

static void _tcp_updateCongestionWindow(TCP* tcp, guint nPacketsAcked) {
	MAGIC_ASSERT(tcp);

	if(tcp->isSlowStart) {
		/* threshold not set => no timeout yet => slow start phase 1
		 *  i.e. multiplicative increase until retransmit event (which sets threshold)
		 * threshold set => timeout => slow start phase 2
		 *  i.e. multiplicative increase until threshold */
		tcp->congestion.window += ((gdouble)nPacketsAcked);
		if(tcp->congestion.threshold != 0 && tcp->congestion.window >= tcp->congestion.threshold) {
			tcp->isSlowStart = FALSE;
		}
	} else {
		/* slow start is over
		 * simple additive increase part of AIMD */
		tcp->congestion.window += (gdouble)(nPacketsAcked * ((gdouble)(nPacketsAcked / tcp->congestion.window)));
	}
}

static Packet* _tcp_createPacket(TCP* tcp, enum ProtocolTCPFlags flags, gconstpointer payload, gsize payloadLength) {
	MAGIC_ASSERT(tcp);

	/*
	 * packets from children of a server must appear to be coming from the server
	 * @todo: does this handle loopback?
	 */
	in_addr_t sourceIP = (tcp->child != NULL) ? tcp->child->parent->super.super.boundAddress :
			tcp->super.super.boundAddress;
	in_port_t sourcePort = (tcp->child != NULL) ? tcp->child->parent->super.super.boundPort :
			tcp->super.super.boundPort;

	/* make sure our receive window is up to date before putting it in the packet */
	_tcp_updateReceiveWindow(tcp);

	/* control packets have no sequence number */
	guint sequence = payloadLength > 0 ? tcp->send.next : 0;

	/* create the TCP packet */
	Packet* packet = packet_new(payload, payloadLength);
	packet_setTCP(packet, flags, sourceIP, sourcePort, tcp->super.peerIP,
			tcp->super.peerPort, tcp->send.next, tcp->receive.next, tcp->receive.window);

	/* update sequence numbers */
	if(sequence > 0) {
		tcp->send.next++;
		tcp->send.end++;
	}

	return packet;
}

static gboolean _tcp_handleOutputPacket(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);

	gsize space = tcp->super.super.outputBufferSize - tcp->super.super.outputBufferLength;
	guint length = packet_getPayloadLength(packet);

	if(length > space) {
		return FALSE;
	}

	if(_tcp_willThrottlePacket(tcp, packet)) {
		/* TCP wants us to wait for enough acks to send */
		// FIXME adjust space of transport layer, even though we buffer it here
		g_queue_insert_sorted(tcp->throttledDataPackets, packet, (GCompareDataFunc)packet_compareTCPSequence, NULL);
	} else {
		/* sendable now, transport will queue it ASAP */
		gboolean success = transport_addToOutputBuffer((Transport*) tcp, packet);
		/* we have space, so this should always succeed */
		g_assert(success);
	}

	return TRUE;
}

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

	/* create the connection state */
	((Socket*) tcp)->peerIP = ip;
	((Socket*) tcp)->peerPort = port;

	/* send 1st part of 3-way handshake, state->syn_sent */
//	FIXME SYN|CON ?
	Packet* packet = _tcp_createPacket(tcp, PTCP_SYN, NULL, 0);
	gboolean success = transport_addToOutputBuffer((Transport*)tcp, packet);

	if(!success) {
		/* this should never happen, control packets consume no buffer space */
		error("error sending SYN step 1");
	}

	_tcp_setState(tcp, TCPS_SYNSENT);

	/* we dont block, so return EINPROGRESS while waiting for establishment */
	return EINPROGRESS;
}

void tcp_enterServerMode(TCP* tcp, gint backlog) {
	MAGIC_ASSERT(tcp);

	/* we are a server ready to listen, build our server state */
	tcp->server = _tcpserver_new(backlog);

	/* we are now listening for connections */
	_tcp_setState(tcp, TCPS_LISTEN);
}

gint tcp_acceptServerPeer(TCP* tcp, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(tcp);

	/* make sure we are listening and bound to an ip and port */
	if(tcp->state != TCPS_LISTEN || !transport_isBound(&(tcp->super.super))) {
		return EINVAL;
	}

	/* we must be a server to accept child connections */
	if(tcp->server == NULL){
		return EINVAL;
	}

	/* if there are no pending connection ready to accept, dont block waiting */
	if(g_queue_get_length(tcp->server->pending) <= 0) {
		return EWOULDBLOCK;
	}

	/* double check the pending child before its accepted */
	TCPChild* child = g_queue_pop_head(tcp->server->pending);
	MAGIC_ASSERT(child);
	g_assert(child->tcp);

	if(child->state != TCPCS_PENDING || child->tcp->state != TCPS_ESTABLISHED) {
		if(child->tcp->error == TCPE_CONNECTION_RESET) {
			/* close stale socket whose connection was reset before accepted */
			// @FIXME CLOSE SOCKET
		}
		return ECONNABORTED;
	}

	/* better have a peer if we are established */
	g_assert(child->tcp->super.peerIP && child->tcp->super.peerPort);

	/* child now gets "accepted" */
	child->state = TCPCS_ACCEPTED;

	/* update child descriptor status */
	descriptor_adjustStatus(&(child->tcp->super.super.super), DS_ACTIVE|DS_WRITABLE, TRUE);

	/* update server descriptor status */
	if(g_queue_get_length(tcp->server->pending) > 0) {
		descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, TRUE);
	} else {
		descriptor_adjustStatus(&(tcp->super.super.super), DS_READABLE, FALSE);
	}

	return 0;
}

gboolean tcp_processPacket(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);
	return FALSE;
}

void tcp_droppedPacket(TCP* tcp, Packet* packet) {
	MAGIC_ASSERT(tcp);

	/* if we are trying to close, we don't care */
	if(tcp->super.super.super.status == DS_STALE) {
		return;
	}

	/* the packet was "dropped" - this is basically a negative ack.
	 * handle congestion control.
	 * TCP-Reno-like fast retransmit, i.e. multiplicative decrease. */
	tcp->congestion.window /= 2;
	if(tcp->congestion.window < 1) {
		tcp->congestion.window = 1;
	}
	if(tcp->isSlowStart && tcp->congestion.threshold == 0) {
		tcp->congestion.threshold = (guint32) tcp->congestion.window;
	}

	/* buffer or send as appropriate */
	_tcp_handleOutputPacket(tcp, packet);
}

gssize tcp_sendUserData(TCP* tcp, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(tcp);

	/* maximum data we can send network, o/w tcp truncates and only sends 65536*/
	gsize acceptable = MIN(nBytes, 65535);
	gsize space = tcp->super.super.outputBufferSize - tcp->super.super.outputBufferLength;

	/* break data into segments and send each in a packet */
	gsize maxPacketLength = CONFIG_MTU - CONFIG_TCPIP_HEADER_SIZE;
	gsize remaining = MIN(acceptable, space);
	gsize offset = 0;

	/* create as many packets as needed */
	while(remaining > 0) {
		gsize copyLength = MIN(maxPacketLength, remaining);

		/* use helper to create the packet */
		Packet* packet = _tcp_createPacket(tcp, PTCP_ACK, buffer + offset, copyLength);

		/* lets send to transport, or queue it in TCP as appropriate */
		if(_tcp_handleOutputPacket(tcp, packet)) {
			/* maintenance */
			remaining -= copyLength;
			offset += copyLength;
		} else {
			warning("unable to send TCP packet");
			break;
		}
	}

	debug("buffered %lu outbound TCP bytes from user", offset);

	return (gssize) offset;
}

gssize tcp_receiveUserData(TCP* tcp, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(tcp);

	gsize remaining = nBytes;
	gsize bytesCopied = 0;
	gsize offset = 0;
	gsize copyLength = 0;

	while(remaining > 0) {
		/* check if we have a partial packet waiting to get finished */
		if(tcp->partialUserDataPacket) {
			guint partialLength = packet_getPayloadLength(tcp->partialUserDataPacket);
			guint partialBytes = partialLength - tcp->partialOffset;
			g_assert(partialBytes > 0);

			copyLength = MIN(partialBytes, remaining);
			bytesCopied += packet_copyPayload(tcp->partialUserDataPacket, tcp->partialOffset, buffer, copyLength);
			remaining -= copyLength;
			offset += copyLength;

			if(copyLength >= partialBytes) {
				/* we finished off the partial packet */
				packet_unref(tcp->partialUserDataPacket);
				tcp->partialUserDataPacket = NULL;
				tcp->partialOffset = 0;
			} else {
				/* still more partial bytes left */
				tcp->partialOffset += bytesCopied;
				g_assert(remaining == 0);
				break;
			}
		}

		/* get the next buffered packet */
		Packet* packet = transport_removeFromInputBuffer((Transport*)tcp);
		if(!packet) {
			break;
		}

		guint packetLength = packet_getPayloadLength(packet);
		copyLength = MIN(packetLength, remaining);
		bytesCopied += packet_copyPayload(packet, 0, buffer + offset, copyLength);
		remaining -= copyLength;
		offset += copyLength;

		if(copyLength < packetLength) {
			/* we were only able to read part of this packet */
			tcp->partialUserDataPacket = packet;
			tcp->partialOffset = copyLength;
			break;
		} else {
			/* we read the entire packet, and are now finished with it */
			packet_unref(packet);
		}
	}

	debug("user read %lu inbound TCP bytes", bytesCopied);

	return (gssize) (bytesCopied == 0 ? -1 : bytesCopied);
}

void tcp_free(TCP* tcp) {
	MAGIC_ASSERT(tcp);

	while(g_queue_get_length(tcp->throttledDataPackets) > 0) {
		packet_unref(g_queue_pop_head(tcp->throttledDataPackets));
	}
	g_queue_free(tcp->throttledDataPackets);

	if(tcp->child) {
		_tcpchild_free(tcp->child);
	}

	if(tcp->server) {
		_tcpserver_free(tcp->server);
	}

	MAGIC_CLEAR(tcp);
	g_free(tcp);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable tcp_functions = {
	(DescriptorFreeFunc) tcp_free,
	(TransportSendFunc) tcp_sendUserData,
	(TransportReceiveFunc) tcp_receiveUserData,
	(TransportProcessFunc) tcp_processPacket,
	(TransportDroppedPacketFunc) tcp_droppedPacket,
	(SocketIsFamilySupportedFunc) tcp_isFamilySupported,
	(SocketConnectToPeerFunc) tcp_connectToPeer,
	MAGIC_VALUE
};

TCP* tcp_new(gint handle) {
	TCP* tcp = g_new0(TCP, 1);
	MAGIC_INIT(tcp);

	socket_init(&(tcp->super), &tcp_functions, DT_TCPSOCKET, handle);

	/* TODO make config option (cant be less than 1 !!) */
	guint32 initial_window = 10;

	tcp->congestion.window = (gdouble)initial_window;
	tcp->congestion.last = initial_window;
	tcp->send.window = initial_window;
	tcp->receive.window = initial_window;

	/* 0 is saved for representing control packets */
	guint32 initialSequenceNumber = 1;

	tcp->send.unacked = initialSequenceNumber;
	tcp->send.next = initialSequenceNumber;
	tcp->send.end = initialSequenceNumber;
	tcp->send.last1 = initialSequenceNumber;
	tcp->send.last2 = initialSequenceNumber;
	tcp->receive.end = initialSequenceNumber;
	tcp->receive.next = initialSequenceNumber;
	tcp->receive.start = initialSequenceNumber;

	tcp->isSlowStart = TRUE;

	tcp->throttledDataPackets = g_queue_new();

	return tcp;
}
