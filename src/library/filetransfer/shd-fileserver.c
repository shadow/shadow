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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>

#include <string.h>

#include "shd-filetransfer.h"

/* TODO better checking of syscall results like strncpy */

/* these MUST be synced with fileserver_codes */
static const gchar* fileserver_code_strings[] = {
	"FS_SUCCESS", "FS_CLOSED", "FS_ERR_INVALID", "FS_ERR_FATAL", "FS_ERR_BADSD", "FS_ERR_WOULDBLOCK", "FS_ERR_BUFSPACE",
	"FS_ERR_SOCKET", "FS_ERR_BIND", "FS_ERR_LISTEN", "FS_ERR_ACCEPT", "FS_ERR_RECV", "FS_ERR_SEND", "FS_ERR_CLOSE", "FS_ERR_EPOLL"
};

const gchar* fileserver_codetoa(enum fileserver_code fsc) {
	gint index = (gint) fsc;
	if(index >= 0 && index < sizeof(fileserver_code_strings)) {
		return fileserver_code_strings[index];
	} else {
		return NULL;
	}
}


static void fileserver_connection_destroy_cb(gpointer data) {
	/* cant call fileserve_connection_close since we are walking the ht */
	fileserver_connection_tp c = data;

	if(c != NULL) {
		if(c->reply.f != NULL) {
			fclose(c->reply.f);
		}
		close(c->sockd);
		free(c);
	}
}

enum fileserver_code fileserver_start(fileserver_tp fs, gint epolld, in_addr_t listen_addr, in_port_t listen_port,
		gchar* docroot, gint max_connections) {
	/* check user inputs */
	if(fs == NULL || strnlen(docroot, FT_STR_SIZE) == FT_STR_SIZE) {
		return FS_ERR_INVALID;
	}

	/* TODO check for network order */

	/* create the socket and get a socket descriptor */
	gint sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sockd < 0) {
		return FS_ERR_SOCKET;
	}

	/* setup the socket address info, server will listen for incoming
	 * connections on listen_port
	 */
	struct sockaddr_in listener;
	memset(&listener, 0, sizeof(listener));
	listener.sin_family = AF_INET;
	listener.sin_addr.s_addr = listen_addr;
	listener.sin_port = listen_port;

	/* bind the socket to the server port */
	gint result = bind(sockd, (struct sockaddr *) &listener, sizeof(listener));
	if (result < 0) {
		return FS_ERR_BIND;
	}

	/* set as server listening socket */
	result = listen(sockd, max_connections);
	if (result < 0) {
		return FS_ERR_LISTEN;
	}

	/* we have success */
	memset(fs, 0, sizeof(fileserver_t));
	fs->epolld = epolld;
	fs->listen_addr = listen_addr;
	fs->listen_port = listen_port;
	fs->listen_sockd = sockd;
	strncpy(fs->docroot, docroot, FT_STR_SIZE);
	fs->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, fileserver_connection_destroy_cb);
	fs->bytes_sent = 0;
	fs->bytes_received = 0;
	fs->replies_sent = 0;

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = fs->listen_sockd;
	if(epoll_ctl(fs->epolld, EPOLL_CTL_ADD, fs->listen_sockd, &ev) < 0) {
		return FS_ERR_EPOLL;
	}

	return FS_SUCCESS;
}

enum fileserver_code fileserver_shutdown(fileserver_tp fs) {
	/* check user inputs */
	if(fs == NULL) {
		return FS_ERR_INVALID;
	}

	/* destroy the hashtable. this calls the connection destroy function for each. */
	g_hash_table_destroy(fs->connections);

	epoll_ctl(fs->epolld, EPOLL_CTL_DEL, fs->listen_sockd, NULL);
	if(close(fs->listen_sockd) < 0) {
		return FS_ERR_CLOSE;
	} else {
		return FS_SUCCESS;
	}
}

enum fileserver_code fileserver_accept_one(fileserver_tp fs, gint* sockd_out) {
	/* check user inputs */
	if(fs == NULL) {
		return FS_ERR_INVALID;
	}

	/* try to accept a connection */
	gint sockd = accept(fs->listen_sockd, NULL, NULL);
	if(sockd < 0) {
		if(errno == EWOULDBLOCK) {
			return FS_ERR_WOULDBLOCK;
		} else {
			return FS_ERR_ACCEPT;
		}
	}

	/* we just accepted a new connection */
	fileserver_connection_tp c = g_new0(fileserver_connection_t, 1);
	c->sockd = sockd;
	c->state = FS_IDLE;

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = c->sockd;
	if(epoll_ctl(fs->epolld, EPOLL_CTL_ADD, c->sockd, &ev) < 0) {
		return FS_ERR_EPOLL;
	}

	/* in case we have a registered stale connection at this sockd, destroy it
	 * and replace with the new connection.
	 */
	g_hash_table_replace(fs->connections, &(c->sockd), c);

	if(sockd_out != NULL) {
		*sockd_out = sockd;
	}

	return FS_SUCCESS;
}

static void fileserve_connection_close(fileserver_tp fs, fileserver_connection_tp c) {
	epoll_ctl(fs->epolld, EPOLL_CTL_DEL, c->sockd, NULL);
	g_hash_table_remove(fs->connections, &(c->sockd));
}

enum fileserver_code fileserver_activate(fileserver_tp fs, gint sockd) {
	/* check user inputs */
	if(fs == NULL || sockd < 0) {
		return FS_ERR_INVALID;
	}

	/* is this for our listening socket */
	if(sockd == fs->listen_sockd) {
		enum fileserver_code result = fileserver_accept_one(fs, NULL);
		while(result == FS_SUCCESS) {
			result = fileserver_accept_one(fs, NULL);
		}
		return result;
	}

	/* otherwise check for a connection */
	fileserver_connection_tp c = g_hash_table_lookup(fs->connections, &sockd);
	if(c == NULL) {
		return FS_ERR_BADSD;
	}

start:
	/* state machine for handling connections */
	switch (c->state) {

		case FS_IDLE: {
			/* reset current state */
			c->request.buf_write_offset = 0;
			c->request.buf_read_offset = 0;
			c->reply.f = NULL;
			c->reply.f_length = 0;
			c->reply.f_read_offset = 0;
			c->reply.buf_read_offset = 0;
			c->reply.buf_write_offset = 0;

			/* wanting to read next request */
			struct epoll_event ev;
			ev.events = EPOLLIN;
			ev.data.fd = c->sockd;
			epoll_ctl(fs->epolld, EPOLL_CTL_MOD, c->sockd, &ev);

			/* fall through to read */
		}

		case FS_REQUEST: {
			gint space = sizeof(c->request.buf) - c->request.buf_write_offset - 1;
			if(space <= 0) {
				/* the request wont fit in our buffer, just give up */
				c->state = FS_REPLY_404_START;
				goto start;
			}

			ssize_t bytes = recv(c->sockd, c->request.buf + c->request.buf_write_offset, space, 0);

			/* check result */
			if(bytes < 0) {
				if(errno == EWOULDBLOCK) {
					return FS_ERR_WOULDBLOCK;
				} else {
					fileserve_connection_close(fs, c);
					return FS_ERR_RECV;
				}
			} else if(bytes == 0) {
				/* other side closed */
				fileserve_connection_close(fs, c);
				return FS_CLOSED;
			}

			c->request.buf_write_offset += bytes;
			fs->bytes_received += bytes;
			c->request.buf[c->request.buf_write_offset] = '\0';

			/* check if the request is all here */
			gchar* found = strcasestr(c->request.buf, FT_2CRLF);

			if(!found) {
				/* need to read more */
				c->state = FS_REQUEST;
			} else {
				/* extract the file path, check http version */
				gchar* relpath = strcasestr(c->request.buf, "GET ");
				if(relpath == NULL) {
					/* malformed */
					c->state = FS_REPLY_404_START;
					goto start;
				}

				relpath += 4;

				gchar* relpath_end = strcasestr(relpath, " ");
				if(relpath_end == NULL) {
					/* malformed */
					c->state = FS_REPLY_404_START;
					goto start;
				}

				size_t relpath_len = relpath_end - relpath;
				size_t filepath_len = sizeof(c->request.filepath) - 1;

				if(relpath_len == 0 || relpath_len > filepath_len) {
					/* the filename is too long */
					c->state = FS_REPLY_404_START;
					goto start;
				}

				size_t copy_len = relpath_len < filepath_len ? relpath_len : filepath_len;

				strncpy(c->request.filepath, relpath, copy_len);
				c->request.filepath[copy_len] = '\0';

				/* re-enter the state machine so we can reply */
				c->state = FS_REPLY_FILE_START;
				goto start;
			}

			break;
		}

		case FS_REPLY_404_START: {
			/* setup buffer for the reply */
			if(sizeof(c->reply.buf) < FT_HTTP_404_LEN) {
				/* set buffer too small */
				fileserve_connection_close(fs, c);
				return FS_ERR_BUFSPACE;
			}

			strncpy(c->reply.buf, FT_HTTP_404, FT_HTTP_404_LEN);
			c->reply.buf_write_offset = FT_HTTP_404_LEN;
			c->reply.f = NULL;

			c->state = FS_REPLY_SEND;
			goto start;
		}

		case FS_REPLY_FILE_START: {
			/* we dont want to read any more, now we want to write the reply */
			struct epoll_event ev;
			ev.events = EPOLLOUT;
			ev.data.fd = c->sockd;
			epoll_ctl(fs->epolld, EPOLL_CTL_MOD, c->sockd, &ev);

			size_t docroot_len = strnlen(fs->docroot, sizeof(fs->docroot));
			size_t filepath_len = strnlen(c->request.filepath, sizeof(c->request.filepath));

			/* stitch together the filepath */
			size_t len = docroot_len + filepath_len;
			gchar abspath[len + 1];
			strncpy(abspath, fs->docroot, docroot_len);
			strncpy(abspath + docroot_len, c->request.filepath, filepath_len);
			abspath[len] = '\0';

			c->reply.f = fopen(abspath, "r");

			if(c->reply.f == NULL) {
				/* some error, reply with a 404 */
				c->state = FS_REPLY_404_START;
				goto start;
			}

			/* get the file size */
			fseek(c->reply.f, 0, SEEK_END);
			c->reply.f_length = (size_t) ftell(c->reply.f);
			rewind(c->reply.f);

			/* write header to reply buffer */
			gint bytes = snprintf(c->reply.buf, sizeof(c->reply.buf), FT_HTTP_200_FMT, c->reply.f_length);

			if(bytes < 0) {
				/* some kind of output error */
				fileserve_connection_close(fs, c);
				fprintf(stderr, "fileserver fatal error: internal io error\n");
				return FS_ERR_FATAL;
			} else if(bytes >= sizeof(c->reply.buf)) {
				/* truncated, our buffer is way too small, just give up */
				fileserve_connection_close(fs, c);
				return FS_ERR_BUFSPACE;
			}

			c->reply.buf_write_offset = bytes;

			if (c->reply.f_length == 0) {
				/* File is empty, don't try to send contents */
				fclose(c->reply.f);
				c->reply.f = NULL;
				c->state = FS_REPLY_SEND;
				goto start;
			} else {
				/* We need to read and send the file, follow through */
				c->state = FS_REPLY_FILE_CONTINUE;
			}
		}

		case FS_REPLY_FILE_CONTINUE: {
			/* do we have space to read more */
			gint done_reading = c->reply.f_read_offset == c->reply.f_length;
			ssize_t space = sizeof(c->reply.buf) - c->reply.buf_write_offset;

			if(space > 0 && !done_reading) {
				gpointer start = c->reply.buf + c->reply.buf_write_offset;
				size_t bytes = fread(start, 1, space, c->reply.f);

				c->reply.buf_write_offset += bytes;
				c->reply.f_read_offset = (size_t) ftell(c->reply.f);

				if(ferror(c->reply.f) != 0) {
					fileserve_connection_close(fs, c);
					fprintf(stderr, "fileserver fatal error: file io error\n");
					return FS_ERR_FATAL;
				}

				/* TODO we use feof and done_reading variable for the same thing */
				if(feof(c->reply.f) != 0) {
					fclose(c->reply.f);
					c->reply.f = NULL;
					c->state = FS_REPLY_SEND;
				}
			}

			/* fall through and try to send some data */
		}

		case FS_REPLY_SEND: {
			gpointer sendpos = c->reply.buf + c->reply.buf_read_offset;
			size_t sendlen = c->reply.buf_write_offset - c->reply.buf_read_offset;

			ssize_t bytes = send(c->sockd, sendpos, sendlen, 0);

			/* check result */
			if(bytes < 0) {
				if(errno == EWOULDBLOCK) {
					return FS_ERR_WOULDBLOCK;
				} else {
					fileserve_connection_close(fs, c);
					return FS_ERR_SEND;
				}
			} else if(bytes == 0) {
				/* other side closed */
				fileserve_connection_close(fs, c);
				return FS_CLOSED;
			} else {
				c->reply.buf_read_offset += bytes;
				fs->bytes_sent += bytes;
			}

			if(c->reply.buf_read_offset == c->reply.buf_write_offset) {
				/* we've sent everything we can, reset offsets */
				c->reply.buf_read_offset = 0;
				c->reply.buf_write_offset = 0;

				/* we can exit if we've now sent everything */
				if(c->reply.f == NULL) {
					fs->replies_sent++;
					c->state = FS_IDLE;
					break;
				}
			}

			/* try to take in more from the file and/or send more */
			goto start;
		}

		default:
			fprintf(stderr, "fileserver fatal error: unknown connection state\n");
			return FS_ERR_FATAL;
	}

	return FS_SUCCESS;
}
