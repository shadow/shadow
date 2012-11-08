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

#include "shd-tor-control.h"

TorControl torControlData;

void torControl_log(GLogLevelFlags level, const gchar* functionName, gchar* format, ...) {
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

void torControl_createCallback(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay) {
	sleep(millisecondsDelay / 1000);
	callback(data);
}

ShadowFunctionTable torControl_functionTable = {
	NULL,
	&torControl_log,
	&torControl_createCallback,
	NULL,
};

gint main(gint argc, gchar *argv[]) {
	torControlData.shadowlib = &torControl_functionTable;

	torControl_init(&torControlData);

	if(argc < 2) {
		const gchar* USAGE = "TorControl USAGE: controlHostsFile ('hostname:port mode [modeArgs]')\n"
					"\t'circuitBuild hop1 hop2 ... -1'\n";
		torControl_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "%s", USAGE);
		return -1;
	}

	TorControl_Args args;
	args.hostsFilename = g_strdup(argv[1]);

	torControl_new(&args);

	g_free(args.hostsFilename);

	gint epolld = epoll_create(1);
	if(epolld == -1) {
		torControl_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		close(epolld);
		return -1;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT;

	/* watch the server epoll descriptors */
	if(torControlData.epolld) {
		ev.data.fd = torControlData.epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}

	struct epoll_event events[10];
	int nReadyFDs;

	while(TRUE) {
		/* wait for some events */
		nReadyFDs = epoll_wait(epolld, events, 10, 0);
		if(nReadyFDs == -1) {
			torControl_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_wait");
			return -1;
		}

		for(int i = 0; i < nReadyFDs; i++) {
			torControl_activate();
		}
	}

	return 0;
}
