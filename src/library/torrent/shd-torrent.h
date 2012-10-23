/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

#ifndef SHD_TORRENT_H_
#define SHD_TORRENT_H_

#include <glib.h>
#include <shd-library.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>

#include "shd-torrent-server.h"
#include "shd-torrent-client.h"
#include "shd-torrent-authority.h"

#define MAX_EVENTS 10

/**
 *
 */
typedef struct _Torrent Torrent;
struct _Torrent {
	ShadowFunctionTable* shadowlib;
	TorrentServer* server;
	TorrentClient* client;
	TorrentAuthority* authority;
	struct timespec lastReport;
	gint clientDone;
};

Torrent**  torrent_init(Torrent* currentTorrent);
void torrent_new(int argc, char* argv[]);
void torrent_activate();
void torrent_free();


#endif /* SHD_TORRENT_H_ */
