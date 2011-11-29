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
#include <stdint.h>
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
#include "shd-cdf.h"
#include "rand.h"
#include "netinet/tcp.h"

#define __USE_POSIX199309 1
//#define _TCP_REPORTING_ENABLED 1

#define FLOG(file, fmt, ...) fprintf(file, "<%u><%u> " fmt, (guint) time(NULL), (guint) (time(NULL)-EXP_START), ## __VA_ARGS__)
#define LOGE(fmt, ...) FLOG(stderr, fmt, ## __VA_ARGS__)
#define LOGD(fmt, ...) FLOG(stdout, fmt, ## __VA_ARGS__)

guint EXP_START = 0;

#ifdef _TCP_REPORTING_ENABLED
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

static void filegetter_main_log_callback(enum service_filegetter_loglevel level, const gchar* message) {
	if(level == SFG_CRITICAL) {
		LOGE("%s\n", message);
	} else if(level == SFG_WARNING || level == SFG_CRITICAL || level == SFG_NOTICE) {
		LOGD("%s\n", message);
	} else {
	}
}

gint main(gint argc, gchar *argv[])
{
	EXP_START = time(NULL);

	LOGD("parsing args\n");
	if(argc != 9) {
		LOGE("wrong number of args. expected 8\n");
		LOGE("USAGE: http_address http_port socks_address socks_port num_downloads filepath (waittime_cdf_path|none) max_runtime_seconds\n");
		return -1;
	}

	gchar* http_address = argv[1];
	gchar* http_port = argv[2];
	gchar* socks_address = argv[3];
	gchar* socks_port = argv[4];
	gchar* num_downloads = argv[5];
	gchar* filepath = argv[6];
	gchar* waittime_cdf_path = argv[7];
	gchar* max_runtime_seconds = argv[8];

	gint downloads_remaining = atoi(num_downloads);
	time_t endtime = EXP_START + atoi(max_runtime_seconds);

	/* cdf for wait times. */
	CumulativeDistribution* wait_cdf = NULL;
	if(strncmp(waittime_cdf_path, "none", 4) != 0) {
		wait_cdf = cdf_new(0, waittime_cdf_path);
		/* cdf uses rand, make sure we seed it */
		srand((guint)(time(NULL)%UINT32_MAX));
	}

	/* we will do one download at a time, with pauses */
	service_filegetter_single_args_t args;
	args.http_server.host = http_address;
	args.http_server.port = http_port;
	args.socks_proxy.host = socks_address;
	args.socks_proxy.port = socks_port;
	args.num_downloads = "1";
	args.filepath = filepath;
	args.log_cb = &filegetter_main_log_callback;

	service_filegetter_t sfg;
	memset(&sfg, 0, sizeof(service_filegetter_t));

	while(downloads_remaining > 0 && time(NULL) < endtime) {
start_loop:;

		gint sockd = 0;
		enum filegetter_code result;

		result = service_filegetter_start_single(&sfg, &args, &sockd);
		if(result != FG_SUCCESS) {
			LOGE("error starting filegetter service, error code = %s\n", filegetter_codetoa(result));
			break;
		}

		/* watch the socket and finish download */
		fd_set read_sockets;
		fd_set write_sockets;
		FD_ZERO(&read_sockets);
		FD_ZERO(&write_sockets);
		FD_SET(sockd, &read_sockets);
		FD_SET(sockd, &write_sockets);

#ifdef _TCP_REPORTING_ENABLED
		/* setup reporting for TCP kernel info */
		struct tcp_info ti;
		socklen_t ti_len = sizeof(ti);
		memset(&ti, 0, ti_len);

		struct timeval ti_start, ti_now;
		memset(&ti_now, 0, sizeof(ti_now));
		gettimeofday(&ti_start, NULL);
#endif

		result = FG_ERR_INVALID;
		while(result != FG_SUCCESS) {
			gint sel_result = select(sockd+1, &read_sockets, &write_sockets, NULL, NULL);
			if(sel_result < 0) {
				perror("select()");
				goto start_loop;
			}

			result = service_filegetter_activate(&sfg, sockd);

			if(result != FG_SUCCESS && result != FG_ERR_WOULDBLOCK) {
				LOGE("error activating filegetter service, error code = %s\n", filegetter_codetoa(result));
				return -1;
			}

#ifdef _TCP_REPORTING_ENABLED
			/* report some TCP kernel info */
			report(sockd, &ti_start, &ti_now, &ti, &ti_len);
#endif

			/* if its past end time, just quit now insteading of selecting again */
			if(time(NULL) > endtime) {
				service_filegetter_stop(&sfg);
				return 0;
			}
		}

		/* ok, done with that download. lets stop the service */
		downloads_remaining--;
		service_filegetter_stop(&sfg);

		if(wait_cdf != NULL && downloads_remaining > 0) {
			/* wait before downloading next, draw from cdf */
			gdouble milliseconds = cdf_getRandomValue(wait_cdf);
			guint seconds = (guint)(milliseconds / 1000);

			/* if we would end after waking up, just quit now */
			if(time(NULL) + seconds > endtime) {
				break;
			}

			LOGD("sleeping %u seconds before next download...\n", seconds);
			sleep(seconds);
		}
	}

	return 0;
}
