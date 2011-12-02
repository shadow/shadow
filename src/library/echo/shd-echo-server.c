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

EchoServer* echoserver_new(enum EchoProtocol protocol, in_addr_t bindIPAddress, ShadowlibLogFunc log) {
	/* start up the echo server */
	gint sockd;
	struct sockaddr_in server;

	gint flags = protocol == EchoTCP ? SOCK_STREAM : SOCK_DGRAM;
	flags |= SOCK_NONBLOCK;

	/* create the socket and get a socket descriptor */
	if ((sockd = socket(AF_INET, flags, 0)) == ERROR) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error creating socket");
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on port SERVER_LISTEN_PORT
	 */
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = bindIPAddress;
	server.sin_port = htons(ECHO_SERVER_PORT);

	/* bind the socket to the server port */
	if (bind(sockd, (struct sockaddr *) &server, sizeof(server)) == ERROR) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in bind");
	}

	if(protocol == EchoTCP) {
		/* set as server socket */
		if (listen(sockd, 100) == ERROR) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in listen");
		}
	}

	gint epollFileDescriptor;
	if((epollFileDescriptor = epoll_create(1)) == ERROR) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
	} else {
		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.fd = sockd;
		if(epoll_ctl(epollFileDescriptor, EPOLL_CTL_ADD, sockd, &ev) == ERROR) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
		}
	}

	/* store the socket as our listening socket */
	EchoServer* es = g_new0(EchoServer, 1);
	es->protocol = protocol;
	es->listen_sd = sockd;
	es->epollFileDescriptor = epollFileDescriptor;
	return es;
}

void echoserver_free(EchoServer* es) {
	g_assert(es);
	epoll_ctl(es->epollFileDescriptor, EPOLL_CTL_DEL, es->listen_sd, NULL);
	g_free(es);
}

static void echoserver_socketReadable(EchoServer* es, gint socketDescriptor, ShadowlibLogFunc log) {
	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "trying to read socket %i", socketDescriptor);

	if(socketDescriptor == es->listen_sd && es->protocol == EchoTCP) {
		/* need to accept a connection on server listening socket,
		 * dont care about address of connector.
		 * this gives us a new socket thats connected to the client */
		gint acceptedDescriptor = 0;
		if((acceptedDescriptor = accept(es->listen_sd, NULL, NULL)) == ERROR) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error accepting socket");
			return;
		}
		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.fd = acceptedDescriptor;
		if(epoll_ctl(es->epollFileDescriptor, EPOLL_CTL_ADD, acceptedDescriptor, &ev) == ERROR) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
		}
	} else {
		socklen_t len = sizeof(es->address);

		/* read all data available */
		gint read_size = BUFFERSIZE - es->read_offset;
		if(read_size > 0) {
		    ssize_t bread = recvfrom(socketDescriptor, es->echo_buffer + es->read_offset, read_size, 0, (struct sockaddr*)&es->address, &len);

			/* if we read, start listening for when we can write */
			if(bread == 0) {
				close(es->listen_sd);
				close(socketDescriptor);
			} else if(bread > 0) {
				log(G_LOG_LEVEL_INFO, __FUNCTION__, "server socket %i read %i bytes", socketDescriptor, (gint)bread);
				es->read_offset += bread;
				read_size -= bread;

				struct epoll_event ev;
				ev.events = EPOLLIN|EPOLLOUT;
				ev.data.fd = socketDescriptor;
				if(epoll_ctl(es->epollFileDescriptor, EPOLL_CTL_MOD, socketDescriptor, &ev) == ERROR) {
					log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
				}
			}
		}
	}

}

static void echoserver_socketWritable(EchoServer* es, gint socketDescriptor, ShadowlibLogFunc log) {
	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "trying to read socket %i", socketDescriptor);

	socklen_t len = sizeof(es->address);

	/* echo it back to the client on the same sd,
	 * also taking care of data that is still hanging around from previous reads. */
	gint write_size = es->read_offset - es->write_offset;
	if(write_size > 0) {
		ssize_t bwrote = sendto(socketDescriptor, es->echo_buffer + es->write_offset, write_size, 0, (struct sockaddr*)&es->address, len);
		if(bwrote == 0) {
			if(epoll_ctl(es->epollFileDescriptor, EPOLL_CTL_DEL, socketDescriptor, NULL) == ERROR) {
				log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
			}
		} else if(bwrote > 0) {
			log(G_LOG_LEVEL_INFO, __FUNCTION__, "server socket %i wrote %i bytes", socketDescriptor, (gint)bwrote);
			es->write_offset += bwrote;
			write_size -= bwrote;
		}
	}

	if(write_size == 0) {
		/* stop trying to write */
		struct epoll_event ev;
		ev.events = EPOLLIN;
		ev.data.fd = socketDescriptor;
		if(epoll_ctl(es->epollFileDescriptor, EPOLL_CTL_MOD, socketDescriptor, &ev) == ERROR) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
		}
	}
}

void echoserver_ready(EchoServer* es, ShadowlibLogFunc log) {
	if(es == NULL) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "NULL server");
		return;
	}

	struct epoll_event events[MAX_EVENTS];
	int nfds = epoll_wait(es->epollFileDescriptor, events, MAX_EVENTS, 0);
	if(nfds == -1) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in epoll_wait");
	}

	for(int i = 0; i < nfds; i++) {
		if(events[i].events & EPOLLIN) {
			echoserver_socketReadable(es, events[i].data.fd, log);
		}
		if(events[i].events & EPOLLOUT) {
			echoserver_socketWritable(es, events[i].data.fd, log);
		}
	}

	if(es->read_offset == es->write_offset) {
		es->read_offset = 0;
		es->write_offset = 0;
	}

	/* cant close sockd to client if we havent received everything yet.
	 * keep it simple and just keep the socket open for now.
	 */
}
