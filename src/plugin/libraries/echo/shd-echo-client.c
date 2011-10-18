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

#include "shd-echo.h"

EchoClient* echoclient_new(in_addr_t serverIPAddress, ShadowlibLogFunc log) {
	EchoClient* ec = g_new0(EchoClient, 1);
	gint sockd = 0;
	struct sockaddr_in server;

	/* setup the socket address info, client has outgoing connection to server */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = serverIPAddress;
	server.sin_port = htons(ECHO_SERVER_PORT);

	/* create the socket and get a socket descriptor */
	if ((sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == ERROR) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in socket");
	}

	/* connect to server. since we cannot block, shadow will notify us via
	 * _module_socket_writable when the connection is established
	 */
	if (!(connect(sockd,(struct sockaddr *)  &server, sizeof(server)) == ERROR
			&& errno == EINPROGRESS)) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in connect");
	}

	ec->sd = sockd;
	return ec;
}

void echoclient_free(EchoClient* ec) {
	g_assert(ec);
	g_free(ec);
}

void echoclient_socketReadable(EchoClient* ec, gint sockd, ShadowlibLogFunc log) {
	if(ec == NULL) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "NULL client");
		return;
	}

	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "trying to read socket %i", sockd);

	if(!ec->is_done) {
		ssize_t b = 0;
		while(ec->amount_sent-ec->recv_offset > 0 &&
				(b = read(sockd, ec->recv_buffer+ec->recv_offset, ec->amount_sent-ec->recv_offset)) > 0) {
			log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "client socket %i read %i bytes: '%s'", sockd, b, ec->recv_buffer+ec->recv_offset);
			ec->recv_offset += b;
		}

		if(ec->recv_offset >= ec->amount_sent) {
			ec->is_done = 1;
			if(memcmp(ec->send_buffer, ec->recv_buffer, ec->amount_sent)) {
				log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "inconsistent echo received!");
			} else {
				log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "consistent echo received!");
			}
			close(sockd);
		} else {
			log(G_LOG_LEVEL_INFO, __FUNCTION__, "echo progress: %i of %i bytes", ec->recv_offset, ec->amount_sent);
		}
	}
}

/* fills buffer with size random characters */
static void echoclient_fillCharBuffer(gchar* buffer, gint size) {
	for(gint i = 0; i < size; i++) {
		gint n = rand() % 26;
		buffer[i] = 'a' + n;
	}
}

void echoclient_socketWritable(EchoClient* ec, gint sockd, ShadowlibLogFunc log) {
	if(ec == NULL) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "NULL client");
		return;
	}

	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "trying to write to socket %i", sockd);

	if(!ec->sent_msg) {
		echoclient_fillCharBuffer(ec->send_buffer, sizeof(ec->send_buffer)-1);
		ssize_t b = write(sockd, ec->send_buffer, sizeof(ec->send_buffer));
		ec->sent_msg = 1;
		ec->amount_sent = b;
		log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "client socket %i wrote %i bytes: '%s'", sockd, b, ec->send_buffer);
	}
}
