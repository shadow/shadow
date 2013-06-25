/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <fcntl.h>
#include <unistd.h>
#include "shd-echo.h"

EchoPipe* echopipe_new(ShadowLogFunc log) {
	EchoPipe* epipe = g_new0(EchoPipe, 1);

	epipe->log = log;

	gint fds[2];
	gint pipeResult = pipe(fds);
	if(pipeResult < 0) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in pipe");
		return NULL;
	}

	epipe->readfd = fds[0];
	epipe->writefd = fds[1];

	/* create an epoll so we can wait for IO events */
	gint epolld = epoll_create(1);
	if(epolld == -1) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		close(epipe->readfd);
		close(epipe->writefd);
		return NULL;
	}

	/* setup the events we will watch for */
	struct epoll_event evr, evw;
	evr.events = EPOLLIN;
	evr.data.fd = epipe->readfd;
	evw.events = EPOLLOUT;
	evw.data.fd = epipe->writefd;

	/* start watching */
	gint resultr = epoll_ctl(epolld, EPOLL_CTL_ADD, epipe->readfd, &evr);
	gint resultw = epoll_ctl(epolld, EPOLL_CTL_ADD, epipe->writefd, &evw);
	if(resultr == -1 || resultw == -1) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
		close(epolld);
		close(epipe->readfd);
		close(epipe->writefd);
		return NULL;
	}

	epipe->epolld = epolld;

	return epipe;
}

void echopipe_free(EchoPipe* epipe) {
	g_assert(epipe);
	close(epipe->epolld);
	g_free(epipe);
}

/* fills buffer with size random characters */
static void _echopipe_fillCharBuffer(gchar* buffer, gint size) {
	for(gint i = 0; i < size; i++) {
		gint n = rand() % 26;
		buffer[i] = 'a' + n;
	}
}

void echopipe_ready(EchoPipe* epipe) {
	g_assert(epipe);

	if(epipe->didRead && epipe->didWrite) {
		return;
	}

	struct epoll_event events[MAX_EVENTS];

	int nfds = epoll_wait(epipe->epolld, events, MAX_EVENTS, 0);
	if(nfds == -1) {
		epipe->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in epoll_wait");
	}

	gint ioResult = 0;

	for(int i = 0; i < nfds; i++) {
		gint socketd = events[i].data.fd;

		if(!(epipe->didRead) && (events[i].events & EPOLLIN)) {
			ioResult = read(socketd, epipe->outputBuffer, BUFFERSIZE);
			if(ioResult < 0) {
				epipe->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "read returned < 0");
			}

			if(memcmp(epipe->inputBuffer, epipe->outputBuffer, BUFFERSIZE)) {
				epipe->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "inconsistent echo received!");
			} else {
				epipe->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "consistent echo received!");
			}

			if(epoll_ctl(epipe->epolld, EPOLL_CTL_DEL, socketd, NULL) == -1) {
				epipe->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
			}

			close(socketd);
			epipe->didRead = TRUE;
		}

		if(!(epipe->didWrite) && (events[i].events & EPOLLOUT)) {
			_echopipe_fillCharBuffer(epipe->inputBuffer, BUFFERSIZE);
			ioResult = write(socketd, (gconstpointer) epipe->inputBuffer, BUFFERSIZE);
			if(ioResult < 0) {
				epipe->log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "write returned < 0");
			}

			if(epoll_ctl(epipe->epolld, EPOLL_CTL_DEL, socketd, NULL) == -1) {
				epipe->log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_ctl");
			}

			close(socketd);
			epipe->didWrite = TRUE;
		}
	}
}
