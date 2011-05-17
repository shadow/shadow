/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "global.h"
#include "socketset.h"

socketset_tp socketset_create(void) {
	socketset_tp ss;

	ss = malloc(sizeof(*ss));
	if(!ss)
		printfault(EXIT_NOMEM, "socketset_create: Out of memory");

	ss->sockets = vector_create();
	ss->fds = vector_create();
	ss->allocated = 0;
	ss->maxfd = 0;
	ss->num_entries = 0;

	FD_ZERO(&ss->master_read_fds);
	FD_ZERO(&ss->readfds);
	FD_ZERO(&ss->writefds);

	return ss;
}

void socketset_destroy(socketset_tp socketset) {
	if(socketset) {
		vector_destroy(socketset->sockets);
		vector_destroy(socketset->fds);

		free(socketset);
	}
}

void socketset_watch_readfd (socketset_tp ss, int fd) {
	if(fd < 0)
		return;

	FD_SET(fd, &ss->master_read_fds);

	if(fd > ss->maxfd)
		ss->maxfd = fd;
}

void socketset_drop_readfd (socketset_tp ss, int fd) {
	FD_CLR(fd, &ss->master_read_fds);
}

void socketset_watch (socketset_tp ss, socket_tp sock) {
	if(!sock)
		return;

	vector_push(ss->sockets, sock);
	FD_SET(socket_getfd(sock), &ss->master_read_fds);

	if(socket_getfd(sock) > ss->maxfd)
		ss->maxfd = socket_getfd(sock);

	return;
}

void socketset_drop (socketset_tp ss, socket_tp sock)  {
	if(!sock)
		return;

	for(int i=0; i < vector_size(ss->sockets); i++) {
		if( vector_get(ss->sockets, i) == sock) {
			vector_remove(ss->sockets, i);
			return;
		}
	}

	FD_CLR(socket_getfd(sock), &ss->master_read_fds);

	return;
}

int socketset_update(socketset_tp ss, struct timeval * timeout, int writes_only) {
	char destroy;
	int work_left = 0;

	FD_ZERO(&ss->readfds); FD_ZERO(&ss->writefds);

	if(!writes_only)
		ss->readfds = ss->master_read_fds;

	/* build our write descriptor sets */
	for(int i=0; i<vector_size(ss->sockets); i++) {
		socket_tp sock = vector_get(ss->sockets, i);

		if( !socket_isvalid(sock) )
			continue;

		if( socket_data_outgoing(sock) ) {
			FD_SET(socket_getfd(sock), &ss->writefds);
			work_left++;
		}
	}

	/* if they want writes only, and theres no work left to do, return right away. */
	if(writes_only && !work_left)
		return 0;

	select(ss->maxfd+1,&ss->readfds,&ss->writefds,NULL,timeout);

	for(int i=0; i<vector_size(ss->sockets); i++) {
		socket_tp sock = vector_get(ss->sockets, i);
		destroy = 0;

		/* first flush out any writes */
		if( FD_ISSET(socket_getfd(sock), &ss->writefds) )
			destroy += (socket_issue_write(sock) == 0 ? 1 : 0);

		/* pull in any reads */
		if( FD_ISSET(socket_getfd(sock), &ss->readfds) && !socket_islisten(sock) )
			destroy += (socket_issue_read(sock) == 0 ? 1 : 0);

		if(destroy)
			socket_close(sock);

		else if(socket_data_outgoing(sock))
			work_left++;
	}

	return work_left;
}
