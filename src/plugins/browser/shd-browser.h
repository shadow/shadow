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
