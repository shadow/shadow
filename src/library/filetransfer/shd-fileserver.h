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

#ifndef SHD_FILESERVER_H_
#define SHD_FILESERVER_H_

#include <glib.h>
#include <stddef.h>
#include <stdio.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>

#include "shd-filetransfer-defs.h"

/*
 * A minimal http server.
 */

/* TODO explain what these codes mean.
 * Note - they MUST be synced with fileserver_code_strings */
enum fileserver_code {
	FS_SUCCESS, FS_CLOSED, FS_ERR_INVALID, FS_ERR_FATAL, FS_ERR_BADSD, FS_ERR_WOULDBLOCK, FS_ERR_BUFSPACE,
	FS_ERR_SOCKET, FS_ERR_BIND, FS_ERR_LISTEN, FS_ERR_ACCEPT, FS_ERR_RECV, FS_ERR_SEND, FS_ERR_CLOSE, FS_ERR_EPOLL,
};

enum fileserver_state {
	FS_IDLE, FS_REQUEST, FS_REPLY_404_START, FS_REPLY_FILE_START, FS_REPLY_FILE_CONTINUE, FS_REPLY_SEND
};

typedef struct fileserver_reply_s {
	FILE* f;
	size_t f_length;
	size_t f_read_offset;
	gchar buf[FT_BUF_SIZE];
	size_t buf_read_offset;
	size_t buf_write_offset;
} fileserver_reply_t, *fileserver_reply_tp;

typedef struct fileserver_request_s {
	gchar filepath[FT_STR_SIZE];
	gchar buf[FT_STR_SIZE];
	size_t buf_read_offset;
	size_t buf_write_offset;
} fileserver_request_t, *fileserver_request_tp;

typedef struct fileserver_connection_s {
	/* this connections socket */
	gint sockd;
	/* the current request we are handling */
	fileserver_request_t request;
	/* the current reply we are sending */
	fileserver_reply_t reply;
	/* keep our state so we know what to do next */
	enum fileserver_state state;
} fileserver_connection_t, *fileserver_connection_tp;

typedef struct fileserver_s {
	in_addr_t listen_addr;
	in_port_t listen_port;
	gint listen_sockd;
	gint epolld;
	gchar docroot[FT_STR_SIZE];
	/* client connections keyed by sockd */
	GHashTable *connections;
	/* global stats for this server */
	size_t bytes_received;
	size_t bytes_sent;
	size_t replies_sent;
} fileserver_t, *fileserver_tp;

/* init and start the fileserver
 *
 * fs must not be null, addr and port are in network order, docroot must be at
 * most FS_STRBUFFER_SIZE including the null byte.
 */
enum fileserver_code fileserver_start(fileserver_tp fs, gint epolld, in_addr_t listen_addr, in_port_t listen_port,
		gchar* docroot, gint max_connections);

/* trys to accept a single connection from the listening socket.
 * if sockd_out is not NULL and the accept succeeds, the sockd will be copied there. */
enum fileserver_code fileserver_accept_one(fileserver_tp fs, gint* sockd_out);

/* if given the fileserver's listening sockd, will try to accept as many
 * connections as it can, and returns the result of the first attempt that does
 * not return FS_SUCCESS.
 *
 * otherwise, handles the connection associated with sockd, if any, by replying
 * with the requested content or 404 errors.
 */
enum fileserver_code fileserver_activate(fileserver_tp fs, gint sockd);

/* close all connections and shutdown the fileserver */
enum fileserver_code fileserver_shutdown(fileserver_tp fs);

/* helpful function for getting a code as ascii. returns NULL on an invalid code. */
const gchar* fileserver_codetoa(enum fileserver_code fsc);

#endif /* SHD_FILESERVER_H_ */
