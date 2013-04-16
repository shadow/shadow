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


#ifndef SHD_FILEGETTER_H_
#define SHD_FILEGETTER_H_

#include <glib.h>
#include <stddef.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <time.h>

#include "shd-filetransfer-defs.h"

/* TODO explain what these codes mean.
 * Note - they MUST be synced with fileserver_code_strings */
enum filegetter_code {
	FG_SUCCESS, FG_ERR_INVALID, FG_ERR_FATAL,
	FG_ERR_NOTSTARTED, FG_ERR_NEEDFSPEC,
	FG_ERR_SOCKET, FG_ERR_SOCKSINIT, FG_ERR_SOCKSCONN, FG_ERR_HTTPCONN, FG_ERR_FOPEN, FG_ERR_CLOSE,
	FG_ERR_WOULDBLOCK, FG_ERR_SEND, FG_ERR_RECV, FG_CLOSED,
	FG_OK_200, FG_ERR_404
};

enum filegetter_state {
	FG_IDLE, FG_SPEC, FG_SEND, FG_RECEIVE, FG_CHECK_DOWNLOAD,
	FG_REQUEST_SOCKS_INIT, FG_TOREPLY_SOCKS_INIT, FG_REPLY_SOCKS_INIT,
	FG_REQUEST_SOCKS_CONN, FG_TOREPLY_SOCKS_CONN, FG_REPLY_SOCKS_CONN,
	FG_REQUEST_HTTP, FG_TOREPLY_HTTP, FG_REPLY_HTTP
};

typedef struct filegetter_filestats_s {
	struct timespec first_byte_time;
	struct timespec download_time;
	size_t body_bytes_downloaded;
	size_t body_bytes_expected;
	size_t bytes_downloaded;
	size_t bytes_uploaded;
} filegetter_filestats_t, *filegetter_filestats_tp;

typedef struct filegetter_filespec_s {
	gchar remote_path[FT_STR_SIZE];
	gchar local_path[FT_STR_SIZE];
	guint8 do_save;
	gboolean save_to_memory;
} filegetter_filespec_t, *filegetter_filespec_tp;

typedef struct filegetter_serverspec_s {
	gchar http_hostname[FT_STR_SIZE];
	in_addr_t http_addr;
	in_port_t http_port;
	in_addr_t socks_addr;
	in_port_t socks_port;
	gboolean persistent;
} filegetter_serverspec_t, *filegetter_serverspec_tp;

typedef struct filegetter_s {
	filegetter_serverspec_t sspec;
	filegetter_filespec_t fspec;
	filegetter_filestats_t curstats;
	filegetter_filestats_t allstats;
	gint sockd;
	gint epolld;
	FILE* f;
	GString* content;
	gchar buf[FT_BUF_SIZE];
	size_t buf_write_offset;
	size_t buf_read_offset;
	struct timespec download_start;
	struct timespec download_first_byte;
	struct timespec download_end;
	enum filegetter_state state;
	enum filegetter_state nextstate;
	enum filegetter_code errcode;
} filegetter_t, *filegetter_tp;

enum filegetter_code filegetter_start(filegetter_tp fg, gint epolld);

enum filegetter_code filegetter_download(filegetter_tp fg, filegetter_serverspec_tp sspec, filegetter_filespec_tp fspec);

enum filegetter_code filegetter_activate(filegetter_tp fg);

enum filegetter_code filegetter_shutdown(filegetter_tp fg);

enum filegetter_code filegetter_stat_download(filegetter_tp fg, filegetter_filestats_tp stats_out);

enum filegetter_code filegetter_stat_aggregate(filegetter_tp fg, filegetter_filestats_tp stats_out);

const gchar* filegetter_codetoa(enum filegetter_code fgc);

#endif /* SHD_FILEGETTER_H_ */
