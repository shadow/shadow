/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <netinet/in.h>
#include <sys/epoll.h>

#include "shd-torcontrol.h"

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
		const gchar* USAGE = "TorControl USAGE:\n"
				"\tsingle hostname port [module moduleArgs]\n"
				"\tmulti controlHostsFile\n\n"
				"available modules:\n"
				"\t'circuitBuild node1,node2,...,nodeN'\n"
				"\t'log'\n";
		torControl_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "%s", USAGE);
		return -1;
	}

	TorControl_Args args;
	args.mode = g_strdup(argv[1]);
	args.argc = argc - 2;
	args.argv = NULL;
	if(argc > 2) {
		args.argv = &argv[2];
	}

	torControl_new(&args);

	g_free(args.mode);

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
