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

#ifndef SHD_BROWSER_H_
#define SHD_BROWSER_H_

#include <unistd.h> /* close */
#include <string.h> /* memset */
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <netdb.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <assert.h>
#include <time.h>
#include <shd-library.h>

#include "shd-html.h"
#include "shd-url.h"
#include "shd-filegetter.h"

enum browser_state {
	SB_DOCUMENT, SB_HIBERNATE, SB_EMBEDDED_OBJECTS, SB_SUCCESS, SB_404, SB_FAILURE
};

typedef struct browser_download_tasks_s {
	/* Count of running tasks */
	gint running;
	/* set that contains the paths that were already added to the queue */
	GHashTable* added;
	/* Flag whether hostname could be resolved */
	gboolean reachable;
	/* contains paths to downloads */
	GQueue* pending;
} browser_download_tasks_t, *browser_download_tasks_tp;

typedef struct browser_server_args_s {
	gchar* host;
	gchar* port;
} browser_server_args_t, *browser_server_args_tp;

typedef struct browser_connection_s browser_connection_t, *browser_connection_tp;

typedef struct browser_s { 
	ShadowFunctionTable* shadowlib;
	enum browser_state state;
	gint epolld;
	gchar* first_hostname;
	/* We never change them during simumlation */
	browser_server_args_tp socks_proxy;
	/* hostname (gchar*) -> download tasks (browser_connection_tp) */
	GHashTable* download_tasks;
	/* contains all open connections (browser_connection_t) */
	GHashTable* connections;
	gint max_concurrent_downloads;
	/* statistics */
	size_t bytes_downloaded;
	size_t bytes_uploaded;
	size_t cumulative_size;
	gint document_size;
	gint embedded_downloads_expected;
	gint embedded_downloads_completed;
	struct timespec embedded_start_time;
	struct timespec embedded_end_time;
	browser_connection_tp doc_conn;
} browser_t, *browser_tp;

struct browser_connection_s {
	browser_tp b;
	filegetter_t fg;
	filegetter_filespec_t fspec;
	filegetter_serverspec_t sspec;
};

typedef struct browser_args_s {
	browser_server_args_t http_server;
	browser_server_args_t socks_proxy;
	gchar* max_concurrent_downloads;
	gchar* document_path;
} browser_args_t, *browser_args_tp;

typedef struct browser_activate_result_s {
	browser_connection_tp connection;
	enum filegetter_code code;
} browser_activate_result_t, *browser_activate_result_tp;

void browser_start(browser_tp b, gint argc, gchar** argv);
void browser_activate(browser_tp b, gint sockfd);
gint browser_launch(browser_tp b, browser_args_tp args, gint epolld);
void browser_free(browser_tp b);

#endif /* SHD_BROWSER_H_ */
