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

#ifndef _socketset_h
#define _socketset_h

#include <glib.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "socket.h"
#include "vector.h"

typedef struct socketset_t {
	vector_tp sockets;
	vector_tp fds;

	guint num_entries;
	guint allocated;

	fd_set master_read_fds;
	//fd_set master_write_fds;

	fd_set readfds;
	fd_set writefds;

	gint maxfd;
} socketset_t, * socketset_tp;

#define socketset_is_readset(ss, socket) (FD_ISSET(socket_getfd(socket), &ss->readfds))
#define socketset_is_writeset(ss, socket) (FD_ISSET(socket_getfd(socket), &ss->writefds))

/**
 * creates a socketset
 */
socketset_tp socketset_create(void);
void socketset_destroy(socketset_tp socketset) ;

/**
 * adds the specified socket to this socketset's watchlist
 */
void socketset_watch (socketset_tp ss, socket_tp sock);
void socketset_drop (socketset_tp ss, socket_tp sock);

void socketset_watch_readfd (socketset_tp ss, gint fd);
void socketset_drop_readfd (socketset_tp ss, gint fd);

/**
 * higher-level equivalent of select(). ensures all sockets that have waiting userspace data to
 * be written are checked for writability (and flushes their data when possible). if
 * sockets have readable data waiting, issues read commands to pull their data ginto userspace
 * from the kernel.
 */
gint socketset_update(socketset_tp ss, struct timeval * timeout,gint writes_only);

#endif
