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

typedef struct fileserver_progress_s {
	gint sockd;
	gsize bytes_read;
	gsize bytes_written;
	gsize reply_length;
	gboolean request_done;
	gboolean reply_done;
	gboolean changed;
} fileserver_progress_t, *fileserver_progress_tp;

typedef struct fileserver_reply_s {
	FILE* f;
	size_t f_length;
	size_t f_read_offset;
	gchar buf[FT_BUF_SIZE];
	size_t buf_read_offset;
	size_t buf_write_offset;
	size_t bytes_sent;
	gboolean done;
} fileserver_reply_t, *fileserver_reply_tp;

typedef struct fileserver_request_s {
	gchar filepath[FT_STR_SIZE];
	gchar buf[FT_STR_SIZE];
	size_t buf_read_offset;
	size_t buf_write_offset;
	size_t bytes_received;
	gboolean done;
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
enum fileserver_code fileserver_activate(fileserver_tp fs, gint sockd, fileserver_progress_tp progress);

/* close all connections and shutdown the fileserver */
enum fileserver_code fileserver_shutdown(fileserver_tp fs);

/* helpful function for getting a code as ascii. returns NULL on an invalid code. */
const gchar* fileserver_codetoa(enum fileserver_code fsc);

#endif /* SHD_FILESERVER_H_ */
