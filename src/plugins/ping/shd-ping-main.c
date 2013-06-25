/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "shd-ping.h"

Ping pingData;

void ping_log(ShadowLogLevel level, const gchar* functionName, const gchar* format, ...) {
	va_list vargs;
	va_start(vargs, format);

	if(level == SHADOW_LOG_LEVEL_DEBUG) {
		return;
	}

	GString* newformat = g_string_new(NULL);
	g_string_append_printf(newformat, "[%s] %s", functionName, format);
	g_logv(G_LOG_DOMAIN, (GLogLevelFlags)level, newformat->str, vargs);
	g_string_free(newformat, TRUE);

	va_end(vargs);
}

void ping_createCallback(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay) {
	sleep(millisecondsDelay / 1000);
	callback(data);
}

ShadowFunctionTable ping_functionTable = {
	NULL,
	&ping_log,
	&ping_createCallback,
	NULL,
};

gint main(gint argc, gchar *argv[]) {
	pingData.shadowlib = &ping_functionTable;

	ping_init(&pingData);

	ping_new(argc, argv);

	gint epolld = epoll_create(1);
	if(epolld == -1) {
		ping_log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		close(epolld);
		return -1;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT;

	/* watch the server epoll descriptors */
	if(pingData.epolld) {
		ev.data.fd = pingData.epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}

	struct epoll_event events[10];
	int nReadyFDs;

	while(TRUE) {
		/* wait for some events */
		nReadyFDs = epoll_wait(epolld, events, 10, 0);
		if(nReadyFDs == -1) {
			ping_log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_wait");
			return -1;
		}

		//ping_log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "have %d fds ready", nReadyFDs);
		for(int i = 0; i < nReadyFDs; i++) {
			ping_activate();
		}
	}

	return 0;
}
