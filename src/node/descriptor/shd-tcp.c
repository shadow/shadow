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

struct _TCP {
	Socket super;

	MAGIC_DECLARE;
};

SocketFunctionTable tcp_functions = {
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

void tcp_free(TCP* data) {
	TCP* tcp = data;
	MAGIC_ASSERT(tcp);

	MAGIC_CLEAR(tcp);
	g_free(tcp);
}

void tcp_send(gpointer data) {

}
