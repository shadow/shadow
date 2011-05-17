/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#ifndef SHD_SERVICE_FILEGETTER_H_
#define SHD_SERVICE_FILEGETTER_H_

#include <stddef.h>
#include <time.h>

#include "shd-filetransfer.h"
#include "shd-cdf.h"
#include "orderedlist.h"

enum service_filegetter_loglevel {
	SFG_CRITICAL, SFG_WARNING, SFG_NOTICE, SFG_INFO, SFG_DEBUG
};

enum service_filegetter_state {
	SFG_NONE, SFG_THINKING, SFG_DOWNLOADING, SFG_DONE
};

enum service_filegetter_type {
	SFG_SINGLE, SFG_MULTI
};

typedef void (*service_filegetter_log_cb)(enum service_filegetter_loglevel level, const char* message);
typedef void (*service_filegetter_sleep_cb)(void* sfg, unsigned int seconds);
typedef in_addr_t (*service_filegetter_hostbyname_cb)(const char* hostname);

typedef struct service_filegetter_server_args_s {
	char* host;
	char* port;
} service_filegetter_server_args_t, *service_filegetter_server_args_tp;

typedef struct service_filegetter_single_args_s {
	service_filegetter_server_args_t http_server;
	service_filegetter_server_args_t socks_proxy;
	service_filegetter_log_cb log_cb;
	service_filegetter_hostbyname_cb hostbyname_cb;
	char* num_downloads;
	char* filepath;
} service_filegetter_single_args_t, *service_filegetter_single_args_tp;

typedef struct service_filegetter_multi_args_s {
	char* server_specification_filepath;
	char* thinktimes_cdf_filepath;
	char* runtime_seconds;
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
	orderedlist_tp downloads;
	service_filegetter_download_tp current_download;
	service_filegetter_hostbyname_cb hostbyname_cb;
	service_filegetter_sleep_cb sleep_cb;
	service_filegetter_log_cb log_cb;
	cdf_tp think_times;
	struct timespec wakeup;
	struct timespec expire;
	char log_buffer[1024];
	int downloads_requested;
	int downloads_completed;
} service_filegetter_t, *service_filegetter_tp;

enum filegetter_code service_filegetter_start_single(service_filegetter_tp sfg, service_filegetter_single_args_tp args, int* sockd_out);
enum filegetter_code service_filegetter_start_multi(service_filegetter_tp sfg, service_filegetter_multi_args_tp args, int* sockd_out);
enum filegetter_code service_filegetter_activate(service_filegetter_tp sfg, int sockd);
enum filegetter_code service_filegetter_stop(service_filegetter_tp sfg);

#endif /* SHD_SERVICE_FILEGETTER_H_ */
