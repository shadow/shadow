/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>

#include "shd-filetransfer.h"
#include "netinet/tcp.h"

#define __USE_POSIX199309 1
//#define _FSDEBUG 1

#define LOGE(fmt, ...) fprintf(stderr, fmt, ## __VA_ARGS__)
#define LOGD(fmt, ...) fprintf(stdout, fmt, ## __VA_ARGS__)

#ifdef _FSDEBUG
void report(gint sockd, struct timeval *ti_start, struct timeval *ti_now, struct tcp_info *ti, socklen_t *ti_len)
{
	/* Convert "struct timeval" to fractional seconds. */
    gettimeofday(ti_now, NULL);
    gdouble t = (ti_now->tv_sec - ti_start->tv_sec) + (ti_now->tv_usec - ti_start->tv_usec) / 1e6;
    /* get and report socket vals */
    getsockopt(sockd, SOL_TCP, TCP_INFO, ti, ti_len);
    LOGD("%.6f sockd=%i last_sent=%u last_recv=%u snd_cwnd=%u snd_thrs=%u snd_wndscale=%u, rcv_thrs=%u rtt=%u rtt_var=%u unacked=%u sacked=%u lost=%u retran=%u fackets=%u\n", t, sockd, ti->tcpi_last_data_sent, ti->tcpi_last_data_recv, ti->tcpi_snd_cwnd, ti->tcpi_snd_ssthresh, ti->tcpi_snd_wscale, ti->tcpi_rcv_ssthresh, ti->tcpi_rtt, ti->tcpi_rttvar, ti->tcpi_unacked, ti->tcpi_sacked, ti->tcpi_lost, ti->tcpi_retrans, ti->tcpi_fackets);
}
#endif

gint main(gint argc, gchar *argv[])
{
	LOGD("parsing args\n");
	if(argc != 3) {
		LOGE("wrong number of args. expected 2.\n");
		LOGE("USAGE: listen_port path/to/docroot\n");
		return -1;
	}

	in_addr_t listen_addr = INADDR_ANY;
	in_port_t listen_port = (in_port_t) atoi(argv[1]);
	gchar* docroot = argv[2];

	fileserver_t fs;
	memset(&fs, 0, sizeof(fileserver_t));

	LOGD("starting fileserver on port %u\n", listen_port);
	enum fileserver_code res = fileserver_start(&fs, htonl(listen_addr), htons(listen_port), docroot, 100);

	if(res == FS_SUCCESS) {
		LOGD("fileserver running on at %s:%u\n", inet_ntoa((struct in_addr){listen_addr}),listen_port);
	} else {
		LOGD("fileserver not started! error code = %s\n", fileserver_codetoa(res));
		return -1;
	}

#ifdef _FSDEBUG
	/* report some TCP kernel info */
	struct tcp_info ti;
	socklen_t ti_len = sizeof(ti);
	memset(&ti, 0, ti_len);

	struct timeval ti_start, ti_now;
	memset(&ti_now, 0, sizeof(ti_now));
	gettimeofday(&ti_start, NULL);

	report(fs.listen_sockd, &ti_start, &ti_now, &ti, &ti_len);
#endif


	GQueue *children = g_queue_new();

	/* main loop */
	while(1) {
		fd_set readset;
		FD_ZERO(&readset);
		fd_set writeset;
		FD_ZERO(&writeset);

		/* watch the server for reads */
		FD_SET(fs.listen_sockd, &readset);
		gint max_sockd = fs.listen_sockd;

		/* watch all children for reads and writes */
		GList *child = g_queue_peek_head_link(children);
		while(child != NULL) {
			gint sd = GPOINTER_TO_INT(child->data);
			if(sd > max_sockd){
				max_sockd = sd;
			}
			FD_SET(sd, &readset);
			FD_SET(sd, &writeset);
                        child = child->next;
		}

		gint err = select(max_sockd+1, &readset, &writeset, NULL, NULL);
		if(err < 0) {
			perror("select()");
		}

		if(FD_ISSET(fs.listen_sockd, &readset)) {
			gint next_sockd = 0;
			enum fileserver_code result = fileserver_accept_one(&fs, &next_sockd);

			if(result == FS_SUCCESS) {
				/* keep a list so we can iterate children */
				g_queue_push_tail(children, (gpointer ) ((long)next_sockd));
			}
		}

		/* keep a list so we can remove closed sockets without affecting our iterator */
		GQueue *remove = g_queue_new();

		child = g_queue_peek_head_link(children);
		while(child != NULL) {
			gint sd = GPOINTER_TO_INT(child->data);

			if(FD_ISSET(sd, &readset) || FD_ISSET(sd, &writeset)) {
				enum fileserver_code result = fileserver_activate(&fs, sd);

				if(result != FS_ERR_WOULDBLOCK && result != FS_SUCCESS) {
					g_queue_push_tail(remove, (gpointer ) ((long)sd));
				}
#ifdef _FSDEBUG
				/* report some TCP kernel info */
				report(socketd, &ti_start, &ti_now, &ti, &ti_len);
#endif

				LOGD("fileserver activation result: %s (%zu bytes in, %zu bytes out, %zu replies)\n",
						fileserver_codetoa(result), fs.bytes_received, fs.bytes_sent, fs.replies_sent);
			}
                        child = child->next;
		}

		while(g_queue_get_length(remove) > 0) {
			g_queue_remove(children, g_queue_pop_head(remove));
		}
		g_queue_free(remove);
	}

	g_queue_free(children);

	/* all done */
	LOGD("fileserver stats: %zu bytes in, %zu bytes out, %zu replies\n",
			fs.bytes_received, fs.bytes_sent, fs.replies_sent);

	LOGD("shutting down fileserver\n");

	fileserver_shutdown(&fs);

	return 0;
}
