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

#ifndef SHD_SERVICE_FILEGETTER_H_
#define SHD_SERVICE_FILEGETTER_H_

#include <glib.h>
#include <stddef.h>
#include <time.h>

#include "shd-filetransfer-defs.h"
#include "shd-filegetter.h"
#include "shd-cdf.h"

enum service_filegetter_loglevel {
	SFG_CRITICAL, SFG_WARNING, SFG_NOTICE, SFG_INFO, SFG_DEBUG
};

enum service_filegetter_state {
	SFG_NONE, SFG_THINKING, SFG_DOWNLOADING, SFG_DONE
};

enum service_filegetter_type {
	SFG_SINGLE, SFG_MULTI,
};

typedef void (*service_filegetter_log_cb)(enum service_filegetter_loglevel level, const gchar* message);
typedef void (*service_filegetter_sleep_cb)(gpointer sfg, guint seconds);
typedef in_addr_t (*service_filegetter_hostbyname_cb)(const gchar* hostname);

typedef struct service_filegetter_server_args_s {
	gchar* host;
	gchar* port;
} service_filegetter_server_args_t, *service_filegetter_server_args_tp;

typedef struct service_filegetter_single_args_s {
	service_filegetter_server_args_t http_server;
	service_filegetter_server_args_t socks_proxy;
	service_filegetter_log_cb log_cb;
	service_filegetter_sleep_cb sleep_cb;
	service_filegetter_hostbyname_cb hostbyname_cb;
	gchar* num_downloads;
	gchar* filepath;
} service_filegetter_single_args_t, *service_filegetter_single_args_tp;

typedef struct service_filegetter_multi_args_s {
	gchar* server_specification_filepath;
	gchar* thinktimes_cdf_filepath;
	gchar* runtime_seconds;
	gchar* num_downloads;
	service_filegetter_server_args_t socks_proxy;
	service_filegetter_hostbyname_cb hostbyname_cb;
	service_filegetter_sleep_cb sleep_cb;
	service_filegetter_log_cb log_cb;
} service_filegetter_multi_args_t, *service_filegetter_multi_args_tp;

typedef struct service_filegetter_download_s {
	filegetter_filespec_t fspec;
	filegetter_serverspec_t sspec;
} service_filegetter_download_t, *service_filegetter_download_tp;

typedef struct service_filegetter_s {
	enum service_filegetter_state state;
	enum service_filegetter_type type;
	filegetter_t fg;
	GTree* downloads;
	service_filegetter_download_tp current_download;
	service_filegetter_hostbyname_cb hostbyname_cb;
	service_filegetter_sleep_cb sleep_cb;
	service_filegetter_log_cb log_cb;
	CumulativeDistribution* think_times;
	gint pausetime_seconds;
	struct timespec wakeup;
	struct timespec expire;
	gchar log_buffer[1024];
	gint downloads_requested;
	gint downloads_completed;
} service_filegetter_t, *service_filegetter_tp;

enum filegetter_code service_filegetter_start_single(service_filegetter_tp sfg, service_filegetter_single_args_tp args, gint epolld, gint* sockd_out);
enum filegetter_code service_filegetter_start_multi(service_filegetter_tp sfg, service_filegetter_multi_args_tp args, gint epolld, gint* sockd_out);
enum filegetter_code service_filegetter_activate(service_filegetter_tp sfg, gint sockd);
enum filegetter_code service_filegetter_stop(service_filegetter_tp sfg);

#endif /* SHD_SERVICE_FILEGETTER_H_ */
