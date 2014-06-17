/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */


#include <glib.h>
#include <shd-library.h>
#include "shd-filetransfer.h"

FileTransfer ftmain_globalData;

void ftmain_log(ShadowLogLevel level, const gchar* functionName, const gchar* format, ...) {
	va_list vargs;
	va_start(vargs, format);

	GString* newformat = g_string_new(NULL);
	g_string_append_printf(newformat, "[%s] %s", functionName, format);
	g_logv(G_LOG_DOMAIN, (GLogLevelFlags)level, newformat->str, vargs);
	g_string_free(newformat, TRUE);

	va_end(vargs);
}

void ftmain_createCallback(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay) {
	sleep(millisecondsDelay / 1000);
	callback(data);
}

ShadowFunctionTable ftmain_functionTable = {
	NULL,
	&ftmain_log,
	&ftmain_createCallback,
	NULL,
};

gint main(gint argc, gchar *argv[])
{
	/* setup the functions filetransfer will use in place of the shadow library */
	ftmain_globalData.shadowlib = &ftmain_functionTable;

	/* register the location of our data structure */
	filetransfer_init(&ftmain_globalData);

	/* create the new state according to user inputs */
	filetransfer_new(argc, argv);

	if(!ftmain_globalData.client && !ftmain_globalData.server) {
		return -1;
	}

	/* now we need to watch all the epoll descriptors in our main loop */
	gint epolld = epoll_create(1);
	if(epolld == -1) {
		ftmain_log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "Error in server epoll_create");
		close(epolld);
		return -1;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN|EPOLLOUT;

	/* watch the inner client/server epoll descriptors */
	if(ftmain_globalData.client && ftmain_globalData.client->fg.epolld) {
		ev.data.fd = ftmain_globalData.client->fg.epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}
	if(ftmain_globalData.server && ftmain_globalData.server->epolld) {
		ev.data.fd = ftmain_globalData.server->epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}

	/* main loop on our epoll descriptors that watch the filetransfer epollds */
	struct epoll_event events[10];
	int nReadyFDs;

	while(TRUE) {
		/* wait for some events */
		nReadyFDs = epoll_wait(epolld, events, 10, 0);
		if(nReadyFDs == -1) {
			ftmain_log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "error in client epoll_wait");
			return -1;
		}

		/* activate for every socket thats ready */
		for(int i = 0; i < nReadyFDs; i++) {
			filetransfer_activate();
		}

		/* break out if the client is done */
		if(ftmain_globalData.client && (ftmain_globalData.client->state == SFG_DONE)) {
			break;
		}
	}

	/* cleanup and close */
	if(ftmain_globalData.client && ftmain_globalData.client->fg.epolld) {
		ev.data.fd = ftmain_globalData.client->fg.epolld;
		epoll_ctl(epolld, EPOLL_CTL_DEL, ev.data.fd, &ev);
	}
	if(ftmain_globalData.server && ftmain_globalData.server->epolld) {
		ev.data.fd = ftmain_globalData.server->epolld;
		epoll_ctl(epolld, EPOLL_CTL_DEL, ev.data.fd, &ev);
	}
	close(epolld);

	filetransfer_free();

	return 0;
}
