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

struct _UDP {
	Socket super;

	MAGIC_DECLARE;
};

gboolean udp_isFamilySupported(UDP* udp, sa_family_t family) {
	MAGIC_ASSERT(udp);
	return (family == AF_INET || family == AF_UNSPEC) ? TRUE : FALSE;
}

gint udp_connectToPeer(UDP* udp, in_addr_t ip, in_port_t port, sa_family_t family) {
	MAGIC_ASSERT(udp);

	/* ip/port specifies the default destination for packets */
	if(family == AF_UNSPEC) {
		/* dissolve our existing defaults */
		udp->super.peerIP = 0;
		udp->super.peerPort = 0;
	} else {
		/* set new defaults */
		udp->super.peerIP = ip;
		udp->super.peerPort = port;
	}

	return 0;
}

gboolean udp_pushInPacket(UDP* udp, Packet* packet) {
	MAGIC_ASSERT(udp);
	return FALSE;
}

Packet* udp_pullOutPacket(UDP* udp) {
	MAGIC_ASSERT(udp);
	return NULL;
}

/*
 * this function builds a UDP packet and sends to the virtual node given by the
 * ip and port parameters. this function assumes that the socket is already
 * bound to a local port, no matter if that happened explicitly or implicitly.
 */
gssize udp_sendUserData(UDP* udp, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(udp);

//	gsize maxPacketLength = CONFIG_DATAGRAM_MAX_SIZE;
//	gsize bytesSent = 0;
//	gsize copySize = 0;
//	gsize remaining = nBytes;

	/* check if we have enough space */
	return -1;
}

gssize udp_receiveUserData(UDP* udp, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(udp);
	return -1;
}

void udp_free(UDP* udp) {
	MAGIC_ASSERT(udp);

	MAGIC_CLEAR(udp);
	g_free(udp);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable udp_functions = {
	(DescriptorFreeFunc) udp_free,
	(TransportSendFunc) udp_sendUserData,
	(TransportReceiveFunc) udp_receiveUserData,
	(TransportPushFunc) udp_pushInPacket,
	(TransportPullFunc) udp_pullOutPacket,
	(SocketIsFamilySupportedFunc) udp_isFamilySupported,
	(SocketConnectToPeerFunc) udp_connectToPeer,
	MAGIC_VALUE
};

UDP* udp_new(gint handle) {
	UDP* udp = g_new0(UDP, 1);
	MAGIC_INIT(udp);

	socket_init(&(udp->super), &udp_functions, DT_UDPSOCKET, handle);

	return udp;
}
