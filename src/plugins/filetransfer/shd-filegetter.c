/*
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
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>

#include "shd-filetransfer.h"

/* these MUST be synced with filegetter_codes */
static const gchar* filegetter_code_strings[] = {
	"FG_SUCCESS", "FG_ERR_INVALID", "FG_ERR_FATAL",
	"FG_ERR_NOTSTARTED", "FG_ERR_NEEDFSPEC",
	"FG_ERR_SOCKET", "FG_ERR_SOCKSINIT", "FG_ERR_SOCKSCONN", "FG_ERR_HTTPCONN", "FG_ERR_FOPEN", "FG_ERR_CLOSE",
	"FG_ERR_WOULDBLOCK", "FG_ERR_SEND", "FG_ERR_RECV", "FG_CLOSED",
	"FG_OK_200", "FG_ERR_404"
};

#define FG_ASSERTBUF(fg, retcode) \
	if(bytes < 0) { \
		return filegetter_die(fg, "filegetter fatal error: internal io error\n"); \
	} else if(bytes >= sizeof(fg->buf)) { \
		/* truncated, our buffer is way too small, just give up */ \
		return filegetter_die(fg, "filegetter fatal error: error writing request\n"); \
	}

#define FG_ASSERTIO(fg, retcode, allowed_errno_logic, fg_errcode) \
	/* check result */ \
	if(retcode < 0) { \
		/* its ok if we would have blocked or if we are not connected yet, \
		 * just try again later. */ \
		if((allowed_errno_logic)) { \
			return FG_ERR_WOULDBLOCK; \
		} else { \
			/* some other send error */ \
			fg->errcode = fg_errcode; \
			return filegetter_die(fg, "filegetter fatal error: error in networkio\n"); \
		} \
	} else if(retcode == 0) { \
		/* other side closed */ \
		fg->errcode = FG_CLOSED; \
		return filegetter_die(fg, "filegetter fatal error: server closed\n"); \
	}

#define FG_ASSERTSTATE(fg) \
	assert(fg); \
	assert(fg->state); \
	assert(fg->sockd != 0); \
	assert(fg->buf_read_offset >= 0 && fg->buf_read_offset < sizeof(fg->buf)); \
	assert(fg->buf_write_offset >= 0 && fg->buf_write_offset < sizeof(fg->buf)); \
	assert(fg->buf_write_offset >= fg->buf_read_offset)

static enum filegetter_code filegetter_die(filegetter_tp fg, gchar* msg) {
	filegetter_shutdown(fg);
	fprintf(stderr, "%s", msg);
	return FG_ERR_FATAL;
}

const gchar* filegetter_codetoa(enum filegetter_code fgc) {
	gint index = (gint) fgc;
	if(index >= 0 && index < sizeof(filegetter_code_strings)) {
		return filegetter_code_strings[index];
	} else {
		return NULL;
	}
}

static enum filegetter_code filegetter_connect(filegetter_tp fg, in_addr_t addr, in_port_t port) {
    /* create the socket and get a socket descriptor */
	gint sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

	if (sockd < 0) {
                perror("socket");
		return FG_ERR_SOCKET;
	}

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = addr;
	server.sin_port = port;

	gint result = connect(sockd,(struct sockaddr *) &server, sizeof(server));

	/* nonblocking sockets means inprogress is ok */
	if(result < 0 && errno != EINPROGRESS) {
                perror("connect");
		return FG_ERR_SOCKET;
	}

	fg->sockd = sockd;

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLOUT;
	ev.data.fd = sockd;
	if(epoll_ctl(fg->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		perror("epoll_ctl");
	}

	return FG_SUCCESS;
}

static enum filegetter_code filegetter_disconnect(filegetter_tp fg) {
	gint fclose_err = 0;
	gint close_err = 0;

	/* close destination file */
	if(fg->f != NULL) {
		fclose_err = fclose(fg->f) < 0 ? 1 : 0;
		fg->f = NULL;
	}

	/* close source socket */
	if(fg->sockd != 0) {
		epoll_ctl(fg->epolld, EPOLL_CTL_DEL, fg->sockd, NULL);

		close_err = close(fg->sockd);
		fg->sockd = 0;
	}

	if(close_err != 0 || fclose_err != 0) {
		return FG_ERR_CLOSE;
	} else {
		return FG_SUCCESS;
	}
}

static void filegetter_metrics_first(filegetter_tp fg) {
	/* first byte statistics */
	fg->curstats.first_byte_time.tv_sec = fg->download_first_byte.tv_sec - fg->download_start.tv_sec;
	fg->curstats.first_byte_time.tv_nsec = fg->download_first_byte.tv_nsec - fg->download_start.tv_nsec;
	while(fg->curstats.first_byte_time.tv_nsec < 0) {
		fg->curstats.first_byte_time.tv_sec--;
		fg->curstats.first_byte_time.tv_nsec += 1000000000;
	}
}

static void filegetter_metrics_progress(filegetter_tp fg) {
	struct timespec current_time;
	clock_gettime(CLOCK_REALTIME, &current_time);

	fg->curstats.download_time.tv_sec = current_time.tv_sec - fg->download_start.tv_sec;
	fg->curstats.download_time.tv_nsec = current_time.tv_nsec - fg->download_start.tv_nsec;
	while(fg->curstats.download_time.tv_nsec < 0) {
		fg->curstats.download_time.tv_sec--;
		fg->curstats.download_time.tv_nsec += 1000000000;
	}
}

static void filegetter_metrics_complete(filegetter_tp fg) {
	/* setup completed download statistics */
	fg->allstats.first_byte_time.tv_sec += fg->curstats.first_byte_time.tv_sec;
	fg->allstats.first_byte_time.tv_nsec += fg->curstats.first_byte_time.tv_nsec;
	while(fg->allstats.first_byte_time.tv_nsec >= 1000000000) {
		fg->allstats.first_byte_time.tv_sec++;
		fg->allstats.first_byte_time.tv_nsec -= 1000000000;
	}

	fg->allstats.download_time.tv_sec += fg->curstats.download_time.tv_sec;
	fg->allstats.download_time.tv_nsec += fg->curstats.download_time.tv_nsec;
	while(fg->allstats.download_time.tv_nsec >= 1000000000) {
		fg->allstats.download_time.tv_sec++;
		fg->allstats.download_time.tv_nsec -= 1000000000;
	}
}

enum filegetter_code filegetter_start(filegetter_tp fg, gint epolld) {
	if(fg == NULL) {
		return FG_ERR_INVALID;
	}

	memset(fg, 0, sizeof(filegetter_t));

	/* we need server and file specs next */
	fg->state = FG_SPEC;
	fg->epolld = epolld;

	return FG_SUCCESS;
}

static enum filegetter_code filegetter_set_specs(filegetter_tp fg, filegetter_serverspec_tp sspec, filegetter_filespec_tp fspec) {
	if(fg == NULL || sspec == NULL || fspec == NULL || fg->state != FG_SPEC) {
		return FG_ERR_INVALID;
	}

	fg->sspec = *sspec;
	fg->fspec = *fspec;

	if (fg->fspec.save_to_memory) {
		/* they want us to save what we get to a string */
		fg->content = g_string_new("");
	}
	
	if(fg->fspec.do_save) {
		/* they want us to save what we get to a file */
		fg->f = fopen(fg->fspec.local_path, "w");
		if(fg->f == NULL) {
			return FG_ERR_FOPEN;
		}
	}

	fg->buf_read_offset = 0;
	fg->buf_write_offset = 0;
	fg->curstats.body_bytes_expected = 0;
	fg->curstats.body_bytes_downloaded = 0;
	fg->curstats.bytes_downloaded = 0;
	fg->curstats.bytes_uploaded = 0;
	fg->curstats.download_time.tv_sec = 0;
	fg->curstats.download_time.tv_nsec = 0;
	fg->curstats.first_byte_time.tv_sec = 0;
	fg->curstats.first_byte_time.tv_nsec = 0;

	return FG_SUCCESS;
}

static void filegetter_changeEpoll(filegetter_tp fg, gint eventType){
	struct epoll_event ev;
	ev.events = eventType;
	ev.data.fd = fg->sockd;
	if(epoll_ctl(fg->epolld, EPOLL_CTL_MOD, fg->sockd, &ev) < 0) {
		perror("epoll_ctl");
	}
}

enum filegetter_code filegetter_download(filegetter_tp fg, filegetter_serverspec_tp sspec, filegetter_filespec_tp fspec) {
	enum filegetter_code result = filegetter_set_specs(fg, sspec, fspec);

	/* if connection is still established, we are ready for the HTTP request */
	if (fg->sspec.persistent && fg->sockd > 0) {
		fg->state = FG_REQUEST_HTTP;
		result = FG_SUCCESS;
	} else if (result == FG_SUCCESS) {
		/* start the timer for the download */
		clock_gettime(CLOCK_REALTIME, &fg->download_start);
		
		/* if the server spec has socks info, we connect there.
		 * otherwise we do a direct connection to the fileserver.
		 */
		if (fg->sspec.socks_port > 0 && fg->sspec.socks_addr != htonl(INADDR_NONE)) {
			/* connect to socks server */
			result = filegetter_connect(fg, fg->sspec.socks_addr, fg->sspec.socks_port);

			if(result != FG_SUCCESS) {
				return FG_ERR_SOCKSCONN;
			}

			/* we need a socks init before we do the HTTP request */
			fg->state = FG_REQUEST_SOCKS_INIT;
		} else {
			/* connect to http server */
			result = filegetter_connect(fg, fg->sspec.http_addr, fg->sspec.http_port);

			if(result != FG_SUCCESS) {
				return FG_ERR_HTTPCONN;
			}

			/* ready for the HTTP request */
			fg->state = FG_REQUEST_HTTP;
		}
	}

	filegetter_changeEpoll(fg, EPOLLOUT);
	return result;
}

enum filegetter_code filegetter_activate(filegetter_tp fg) {
	FG_ASSERTSTATE(fg);

start:
	/* our state machine for our get requests */
	switch (fg->state) {

		case FG_IDLE: {
			/* needs to call filegetter_start */
			return FG_ERR_NOTSTARTED;
		}

		case FG_SPEC: {
			/* needs to call filegetter_set_fspec */
			return FG_ERR_NEEDFSPEC;
		}

		case FG_REQUEST_SOCKS_INIT: {
			/* check that we actually have FT_SOCKS_INIT_LEN space */
			assert(sizeof(fg->buf) - fg->buf_write_offset >= FT_SOCKS_INIT_LEN);

			/* write the request to our buffer */
			memcpy(fg->buf + fg->buf_write_offset, FT_SOCKS_INIT, FT_SOCKS_INIT_LEN);

			fg->buf_write_offset += FT_SOCKS_INIT_LEN;

			/* we are ready to send, then transition to socks init reply */
			fg->state = FG_SEND;
			fg->nextstate = FG_TOREPLY_SOCKS_INIT;

			filegetter_changeEpoll(fg, EPOLLOUT);

			goto start;
		}

		case FG_TOREPLY_SOCKS_INIT: {
			filegetter_changeEpoll(fg, EPOLLIN);
			fg->state = FG_RECEIVE;
			fg->nextstate = FG_REPLY_SOCKS_INIT;
			goto start;
		}

		case FG_REPLY_SOCKS_INIT: {
			/* if we didnt get it all, go back for more */
			if(fg->buf_write_offset - fg->buf_read_offset < 2) {
				fg->state = FG_TOREPLY_SOCKS_INIT;
				goto start;
			}

			/* must be version 5 */
			if(fg->buf[fg->buf_read_offset] != 0x05) {
			return FG_ERR_SOCKSINIT;
			}
			/* must be success */
			if(fg->buf[fg->buf_read_offset + 1] != 0x00) {
				return FG_ERR_SOCKSINIT;
			}

			fg->buf_read_offset += 2;

				/* now send the socks connection request */
				fg->state = FG_REQUEST_SOCKS_CONN;
				goto start;
			}

		case FG_REQUEST_SOCKS_CONN: {
			/* check that we actually have FT_SOCKS_REQ_HEAD_LEN+6 space */
			assert(sizeof(fg->buf) - fg->buf_write_offset >= FT_SOCKS_REQ_HEAD_LEN + 6);

			/* write connection request, including intended destination */
			memcpy(fg->buf + fg->buf_write_offset, FT_SOCKS_REQ_HEAD, FT_SOCKS_REQ_HEAD_LEN);
			fg->buf_write_offset += FT_SOCKS_REQ_HEAD_LEN;
			memcpy(fg->buf + fg->buf_write_offset, &(fg->sspec.http_addr), 4);
			fg->buf_write_offset += 4;
			memcpy(fg->buf + fg->buf_write_offset, &(fg->sspec.http_port), 2);
			fg->buf_write_offset += 2;

			/* we are ready to send, then transition to socks conn reply */
			fg->state = FG_SEND;
			fg->nextstate = FG_TOREPLY_SOCKS_CONN;
			filegetter_changeEpoll(fg, EPOLLOUT);

			goto start;
		}

		case FG_TOREPLY_SOCKS_CONN: {
			filegetter_changeEpoll(fg, EPOLLIN);
			fg->state = FG_RECEIVE;
			fg->nextstate = FG_REPLY_SOCKS_CONN;
			goto start;
		}

		case FG_REPLY_SOCKS_CONN: {
			/* if we didnt get it all, go back for more */
			if(fg->buf_write_offset - fg->buf_read_offset < 10) {
				fg->state = FG_TOREPLY_SOCKS_CONN;
				goto start;
			}

			/* must be version 5 */
			if(fg->buf[fg->buf_read_offset] != 0x05) {
			return FG_ERR_SOCKSCONN;
			}

			/* must be success */
			if(fg->buf[fg->buf_read_offset + 1] != 0x00) {
				return FG_ERR_SOCKSCONN;
			}

			/* check address type for IPv4 */
			if(fg->buf[fg->buf_read_offset + 3] != 0x01) {
				return FG_ERR_SOCKSCONN;
			}

			/* get address server told us */
			in_addr_t socks_bind_addr;
			in_port_t socks_bind_port;
			memcpy(&socks_bind_addr, &(fg->buf[fg->buf_read_offset + 4]), 4);
			memcpy(&socks_bind_port, &(fg->buf[fg->buf_read_offset + 8]), 2);

			fg->buf_read_offset += 10;

			/* if we were send a new address, we need to reconnect there */
			if(socks_bind_addr != 0 && socks_bind_port != 0) {
					/* reconnect at new address */
					close(fg->sockd);
					if(filegetter_connect(fg, socks_bind_addr, socks_bind_port) != FG_SUCCESS) {
						return FG_ERR_SOCKSCONN;
					}
			}

			/* now we are ready to send the http request */
			fg->state = FG_REQUEST_HTTP;
			fg->nextstate = FG_REQUEST_HTTP;

			goto start;
			}

		case FG_REQUEST_HTTP: {
			/* write the request to our buffer */
			ssize_t space = sizeof(fg->buf) - fg->buf_write_offset;
			assert(space > 0);
			gint bytes = snprintf(fg->buf + fg->buf_write_offset, (size_t) space, FT_HTTP_GET_FMT, fg->fspec.remote_path, fg->sspec.http_hostname);

			FG_ASSERTBUF(fg, bytes);

			fg->buf_write_offset += bytes;

			/* we are ready to send, then transition to http reply */
			filegetter_changeEpoll(fg, EPOLLOUT);
			fg->state = FG_SEND;
			fg->nextstate = FG_TOREPLY_HTTP;

			goto start;
			}

		case FG_TOREPLY_HTTP: {
			filegetter_changeEpoll(fg, EPOLLIN);
			fg->state = FG_RECEIVE;
			fg->nextstate = FG_REPLY_HTTP;
			goto start;
			}

		case FG_REPLY_HTTP: {
			fg->buf[fg->buf_write_offset] = '\0';

			/* check for status code */
			gchar* err404 = strcasestr(fg->buf + fg->buf_read_offset, FT_HTTP_404);
			if(err404) {
				/* well, that sucks but no file for us */
				fg->buf_read_offset += FT_HTTP_404_LEN;

				/* need another file spec, then send another http req */
				fg->state = FG_SPEC;
				fg->nextstate = FG_REQUEST_HTTP;

				return FG_ERR_404;
			}

			/* check if we have the entire reply header */
			gchar* ok200 = strcasestr(fg->buf + fg->buf_read_offset, FT_HTTP_200);
			gchar* content = strcasestr(fg->buf + fg->buf_read_offset, FT_2CRLF);

			if(!ok200 || !content) {
				/* need more, come back here after */
				fg->state = FG_RECEIVE;
				fg->nextstate = FG_REPLY_HTTP;
				goto start;
			}

			gchar* payload = content + FT_2CRLF_LEN;

			/* so now we have the entire header, extract the content length */
			gchar* cl = strcasestr(fg->buf + fg->buf_read_offset, FT_CONTENT);

			if(!cl) {
				/* malformed reply! */
				return filegetter_die(fg, "filegetter fatal error: malformed http reply\n");
			}

			cl += FT_CONTENT_LEN;
			content[0] = '\0';
			fg->curstats.body_bytes_expected = (size_t) atoi(cl);
			fg->allstats.body_bytes_expected += fg->curstats.body_bytes_expected;

			/* start reading the buf from the payload */
			fg->buf_read_offset = payload - fg->buf;

			/* proceed to finish downloading */
			fg->state = FG_CHECK_DOWNLOAD;
			goto start;
		}

		case FG_SEND: {
			assert(fg->buf_write_offset >= fg->buf_read_offset);

			gpointer sendpos = fg->buf + fg->buf_read_offset;
			size_t sendlen = fg->buf_write_offset - fg->buf_read_offset;

			ssize_t bytes = send(fg->sockd, sendpos, sendlen, 0);

			FG_ASSERTIO(fg, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, FG_ERR_SEND);

			fg->buf_read_offset += bytes;
			fg->curstats.bytes_uploaded += bytes;
			fg->allstats.bytes_uploaded += bytes;

			if(fg->buf_read_offset == fg->buf_write_offset) {
				/* we've sent everything we can, reset offsets */
				fg->buf_read_offset = 0;
				fg->buf_write_offset = 0;

				/* now we go to the next state */
				fg->state = fg->nextstate;
			}

			/* either next state or try to send more */
			goto start;
		}

		case FG_RECEIVE: {
			size_t space = sizeof(fg->buf) - fg->buf_write_offset;

			/* we will recv from socket and write to buf */
			gpointer recvpos = fg->buf + fg->buf_write_offset;
			ssize_t bytes = recv(fg->sockd, recvpos, space, 0);

			FG_ASSERTIO(fg, bytes, errno == EWOULDBLOCK, FG_ERR_RECV);

			fg->buf_write_offset += bytes;
			fg->curstats.bytes_downloaded += bytes;
			fg->allstats.bytes_downloaded += bytes;

			/* go to the next state to check new data */
			fg->state = fg->nextstate;

			goto start;
		}

		case FG_CHECK_DOWNLOAD: {
			/* save any bytes from our buffer, and check if we're done */
			size_t bytes_avail = fg->buf_write_offset - fg->buf_read_offset;

			if(fg->curstats.body_bytes_downloaded == 0 && bytes_avail > 0) {
				/* got first bytes, get timestamp */
				clock_gettime(CLOCK_REALTIME, &fg->download_first_byte);

				/* compute metrics */
				filegetter_metrics_first(fg);
			}

			fg->curstats.body_bytes_downloaded += bytes_avail;
			fg->allstats.body_bytes_downloaded += bytes_avail;

			if(bytes_avail > 0) {
				/* progressed since last time */
				filegetter_metrics_progress(fg);
			}
			
			if (fg->content != NULL) {
				 fg->content = g_string_append_len(fg->content, fg->buf + fg->buf_read_offset, bytes_avail);
			}

			if(fg->f != NULL) {
				size_t bytes_written = fwrite(fg->buf + fg->buf_read_offset, 1, (size_t)bytes_avail, fg->f);

				if(bytes_written != bytes_avail || ferror(fg->f) != 0) {
					return filegetter_die(fg, "filegetter fatal error: file io error\n");
				}
			}

			/* we emptied our buffer */
			fg->buf_write_offset = 0;
			fg->buf_read_offset = 0;

			if(fg->curstats.body_bytes_downloaded >= fg->curstats.body_bytes_expected) {
				/* done downloading, get timestamp */
				clock_gettime(CLOCK_REALTIME, &fg->download_end);

				/* compute metrics */
				filegetter_metrics_complete(fg);

				/* if connection is not supposed to be persistent ... */
				if (!fg->sspec.persistent) {
					/* ... close it */
					filegetter_disconnect(fg);
				}

				/* wait for the next file */
				fg->state = FG_SPEC;

				return FG_OK_200;
			} else {
				/* need to recv more data */
				fg->state = FG_RECEIVE;
				fg->nextstate = FG_CHECK_DOWNLOAD;
				goto start;
			}
		}

		default:
			fprintf(stderr, "filegetter fatal error: unknown connection state\n");
			return FG_ERR_FATAL;
	}

	/* dont think this is reachable */
	return FG_SUCCESS;
}

enum filegetter_code filegetter_shutdown(filegetter_tp fg) {
	if(fg == NULL) {
		return FG_ERR_INVALID;
	}

	fg->state = FG_IDLE;

	return filegetter_disconnect(fg);
}

enum filegetter_code filegetter_stat_download(filegetter_tp fg, filegetter_filestats_tp stats_out){
	if(fg == NULL || stats_out == NULL) {
		return FG_ERR_INVALID;
	}

	*stats_out = fg->curstats;

	return FG_SUCCESS;
}

enum filegetter_code filegetter_stat_aggregate(filegetter_tp fg, filegetter_filestats_tp stats_out) {
	if(fg == NULL || stats_out == NULL) {
		return FG_ERR_INVALID;
	}

	*stats_out = fg->allstats;

	return FG_SUCCESS;
}
