/**
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

#include <glib.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "shd-torrent.h"

Torrent torrentData;

void torrent_log(GLogLevelFlags level, const gchar* functionName, gchar* format, ...) {
	va_list vargs;
	va_start(vargs, format);

	if(level == G_LOG_LEVEL_DEBUG) {
		return;
	}

	GString* newformat = g_string_new(NULL);
	g_string_append_printf(newformat, "[%s] %s", functionName, format);
	g_logv(G_LOG_DOMAIN, level, newformat->str, vargs);
	g_string_free(newformat, TRUE);

	va_end(vargs);
}

void torrent_createCallback(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay) {
	sleep(millisecondsDelay / 1000);
	callback(data);
}

ShadowFunctionTable torrent_functionTable = {
	NULL,
	&torrent_log,
	&torrent_createCallback,
	NULL,
};

gint main(gint argc, gchar *argv[]) {
	torrentData.shadowlib = &torrent_functionTable;

	torrent_init(&torrentData);

	torrent_new(argc, argv);

	if(!torrentData.server && !torrentData.authority) {
		return -1;
	}

	gint epolld = epoll_create(1);
	if(epolld == -1) {
		torrent_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		close(epolld);
		return -1;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT;

	/* watch the server epoll descriptors */
	if(torrentData.server && torrentData.server->epolld) {
		ev.data.fd = torrentData.server->epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}

	/* watch the client epoll descriptors */
	if(torrentData.client && torrentData.client->epolld) {
		ev.data.fd = torrentData.client->epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}

	/* watch the authority epoll descriptors */
	if(torrentData.authority && torrentData.authority->epolld) {
		ev.data.fd = torrentData.authority->epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}

	struct epoll_event events[10];
	int nReadyFDs;

	while(TRUE) {
		/* wait for some events */
		nReadyFDs = epoll_wait(epolld, events, 10, 0);
		if(nReadyFDs == -1) {
			torrent_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_wait");
			return -1;
		}

		for(int i = 0; i < nReadyFDs; i++) {
			torrent_activate();
		}
	}

	return 0;
}
