/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */


#include <glib.h>
#include <shd-library.h>
#include "shd-filetransfer.h"

FileTransfer ftmain_globalData;

void ftmain_log(GLogLevelFlags level, const gchar* functionName, gchar* format, ...) {
	va_list vargs;
	va_start(vargs, format);

	GString* newformat = g_string_new(NULL);
	g_string_append_printf(newformat, "[%s] %s", functionName, format);
	g_logv(G_LOG_DOMAIN, level, newformat->str, vargs);
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
		ftmain_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in server epoll_create");
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
			ftmain_log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in client epoll_wait");
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
