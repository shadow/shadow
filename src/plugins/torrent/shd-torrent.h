/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
