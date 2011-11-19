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

void udp_send(UDP* udp) {

}

void udp_free(UDP* udp) {
	MAGIC_ASSERT(udp);

	MAGIC_CLEAR(udp);
	g_free(udp);
}

/* we implement the socket interface, this describes our function suite */
SocketFunctionTable udp_functions = {
	(SocketIsFamilySupportedFunc) udp_isFamilySupported,
	(SocketConnectToPeerFunc) udp_connectToPeer,
	(SocketSendFunc) udp_send,
	(SocketFreeFunc) udp_free,
	MAGIC_VALUE
};

UDP* udp_new(gint handle) {
	UDP* udp = g_new0(UDP, 1);
	MAGIC_INIT(udp);

	socket_init(&(udp->super), &udp_functions, DT_UDPSOCKET, handle);

	return udp;
}
