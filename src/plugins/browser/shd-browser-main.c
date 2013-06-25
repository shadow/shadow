/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <shd-library.h>
#include "shd-browser.h"

/* my global structure with application state */
browser_t b;

void bmain_log(ShadowLogLevel level, const gchar* functionName, const gchar* format, ...) {
	va_list vargs;
	va_start(vargs, format);

	GString* newformat = g_string_new(NULL);
	g_string_append_printf(newformat, "[%s] %s", functionName, format);
	g_logv(G_LOG_DOMAIN, (GLogLevelFlags)level, newformat->str, vargs);
	g_string_free(newformat, TRUE);

	va_end(vargs);
}

void bmain_createCallback(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay) {
	sleep(millisecondsDelay / 1000);
	callback(data);
}

ShadowFunctionTable bmain_functionTable = {
	NULL,
	&bmain_log,
	&bmain_createCallback,
	NULL,
};

gint main(gint argc, gchar *argv[])
{
	/* setup the functions filetransfer will use in place of the shadow library */
	b.shadowlib = &bmain_functionTable;

	/* Let's download some files */
	browser_start(&b, argc, argv);

	/* now we need to watch all the epoll descriptors in our main loop */
	gint epolld = epoll_create(1);
	if(epolld == -1) {
		bmain_log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "Error in server epoll_create");
		close(epolld);
		return -1;
	}

	struct epoll_event ev;
	ev.events = EPOLLIN|EPOLLOUT;

	/* watch the inner epoll descriptor */
	if(b.epolld) {
		ev.data.fd = b.epolld;
		epoll_ctl(epolld, EPOLL_CTL_ADD, ev.data.fd, &ev);
	}

	/* main loop on our epoll descriptors that watch the filetransfer epollds */
	struct epoll_event events[10];
	int nReadyFDs;

	while(TRUE) {
		/* wait for some events */
		nReadyFDs = epoll_wait(epolld, events, 10, 0);
		if(nReadyFDs == -1) {
			bmain_log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "error in client epoll_wait");
			return -1;
		}

		/* activate for every socket thats ready */
		for(int i = 0; i < nReadyFDs; i++) {
			browser_activate(&b, events[i].data.fd);
		}

		/* break out if the client is done */
		if(b.state == SB_SUCCESS) {
			break;
		}
	}

	/* cleanup and close */
	if(b.epolld) {
		ev.data.fd = b.epolld;
		epoll_ctl(epolld, EPOLL_CTL_DEL, ev.data.fd, &ev);
	}
	
	close(epolld);
	browser_free(&b);

	return 0;
}
