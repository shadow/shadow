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

SocketFunctionTable udp_functions = {
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

void udp_free(UDP* data) {
	UDP* udp = data;
	MAGIC_ASSERT(udp);

	MAGIC_CLEAR(udp);
	g_free(udp);
}

void udp_send(gpointer data) {

}
