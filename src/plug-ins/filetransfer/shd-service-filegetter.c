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

#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <arpa/inet.h>

/* this service implements a filegetter and may be used inside or outside of shadow */
#include "shd-service-filegetter.h"
#include "shd-filetransfer.h"
#include "shd-cdf.h"
#include "orderedlist.h"

static void service_filegetter_log(service_filegetter_tp sfg, enum service_filegetter_loglevel level, const char* format, ...) {
	/* if they gave NULL as a callback, dont log */
	if(sfg != NULL && sfg->log_cb != NULL) {
		va_list vargs, vargs_copy;
		size_t s = sizeof(sfg->log_buffer);

		va_start(vargs, format);
		va_copy(vargs_copy, vargs);
		vsnprintf(sfg->log_buffer, s, format, vargs);
		va_end(vargs_copy);

		sfg->log_buffer[s-1] = '\0';

		(*(sfg->log_cb))(level, sfg->log_buffer);
	}
}

static void service_filegetter_report(service_filegetter_tp sfg, enum service_filegetter_loglevel level, char* preamble, filegetter_filestats_tp stats, int current_download, int total_downloads) {
	if(preamble != NULL && stats != NULL) {
		service_filegetter_log(sfg, level, "%s got first bytes in %lu.%.3d seconds and %zu of %zu bytes in %lu.%.3d seconds (download %i of %i)",
				preamble,
				stats->first_byte_time.tv_sec, (int)(stats->first_byte_time.tv_nsec / 1000000),
				stats->bytes_downloaded, stats->bytes_expected,
				stats->download_time.tv_sec, (int) (stats->download_time.tv_nsec / 1000000),
				current_download, total_downloads);
	}
}

static in_addr_t service_filegetter_getaddr(service_filegetter_tp sfg, service_filegetter_server_args_tp server,
		service_filegetter_hostbyname_cb hostname_cb) {
	/* check if we have an address as a string */
	struct in_addr in;
	int is_ip_address = inet_aton(server->host, &in);

	if(is_ip_address) {
		return in.s_addr;
	} else {
		/* its a hostname, they better have given us the callback */
		if(hostname_cb == NULL) {
			service_filegetter_log(sfg, SFG_CRITICAL, "need to do an address lookup for %s, but the hostbyname callback is NULL", server->host);
			return INADDR_NONE;
		}

		return (*hostname_cb)(server->host);
	}
}

static service_filegetter_download_tp service_filegetter_get_download_from_args(
		service_filegetter_tp sfg, service_filegetter_server_args_tp http_server,
		service_filegetter_server_args_tp socks_proxy, char* filepath,
		service_filegetter_hostbyname_cb hostbyname_cb) {

	/* absolute file path to get from server */
	if(filepath[0] != '/') {
		service_filegetter_log(sfg, SFG_CRITICAL, "filepath %s does not begin with '/'", filepath);
		return NULL;
	}

	/* we require http info */
	if(http_server == NULL) {
		service_filegetter_log(sfg, SFG_CRITICAL, "no HTTP server specified");
		return NULL;
	}

	in_addr_t http_addr = service_filegetter_getaddr(sfg, http_server, hostbyname_cb);
	in_port_t http_port = htons((in_port_t) atoi(http_server->port));
	if(http_addr == 0 || http_port == 0) {
		service_filegetter_log(sfg, SFG_CRITICAL, "HTTP server specified but 0");
		return NULL;
	}

	/* there may not be a socks proxy, so NULL is ok */
	in_addr_t socks_addr = 0;
	in_port_t socks_port = 0;
	if(socks_proxy != NULL) {
		socks_addr = service_filegetter_getaddr(sfg, socks_proxy, hostbyname_cb);
		socks_port = htons((in_port_t) atoi(socks_proxy->port));
	}

	/* validation successful */
	service_filegetter_download_tp dl = calloc(1, sizeof(service_filegetter_download_t));
	strncpy(dl->fspec.remote_path, filepath, sizeof(dl->fspec.remote_path));
	dl->sspec.http_addr = http_addr;
	dl->sspec.http_port = http_port;
	dl->sspec.socks_addr = socks_addr;
	dl->sspec.socks_port = socks_port;

	return dl;
}

static enum filegetter_code service_filegetter_download_next(service_filegetter_tp sfg) {
	assert(sfg);

	switch (sfg->type) {

		case SFG_MULTI: {
			/* get a new random download */
			uint64_t position = (uint64_t) (rand() % orderedlist_length(sfg->downloads));

			sfg->current_download = orderedlist_ceiling_value(sfg->downloads, position);

			if(sfg->current_download == NULL) {
				return FG_ERR_INVALID;
			}

			/* follow through to set the download */
		}

		case SFG_SINGLE:
		case SFG_DOUBLE: {
			enum filegetter_code result = filegetter_download(&sfg->fg, &sfg->current_download->sspec, &sfg->current_download->fspec);
			service_filegetter_log(sfg, SFG_DEBUG, "filegetter set specs code: %s", filegetter_codetoa(result));

			if(result == FG_SUCCESS) {
				sfg->state = SFG_DOWNLOADING;
			}

			return result;
		}

		default: {
			return FG_ERR_INVALID;
		}
	}
}

static enum filegetter_code service_filegetter_launch(service_filegetter_tp sfg, int* sockd_out) {
	/* inputs should be ok, start up the client */
	enum filegetter_code result = filegetter_start(&sfg->fg);
	service_filegetter_log(sfg, SFG_DEBUG, "filegetter startup code: %s", filegetter_codetoa(result));

	/* set our download specifications */
	result = service_filegetter_download_next(sfg);
	if(result == FG_SUCCESS && sockd_out != NULL) {
		/* ready to activate */
		*sockd_out = sfg->fg.sockd;
	}

	return result;
}

enum filegetter_code service_filegetter_start_single(service_filegetter_tp sfg,
		service_filegetter_single_args_tp args, int* sockd_out) {
	assert(sfg);
	assert(args);

	memset(sfg, 0, sizeof(service_filegetter_t));

	sfg->type = SFG_SINGLE;
	sfg->state = SFG_NONE;

	/* if null, we ignore logging */
	sfg->log_cb = args->log_cb;

	/* we download a single file, store our specification in current */
	sfg->current_download = service_filegetter_get_download_from_args(sfg, &args->http_server, &args->socks_proxy, args->filepath, args->hostbyname_cb);
	if(sfg->current_download == NULL) {
		return FG_ERR_INVALID;
	}

	sfg->downloads_requested = atoi(args->num_downloads);
	if(sfg->downloads_requested <= 0) {
		service_filegetter_log(sfg, SFG_WARNING, "you didn't want to download anything?");
		return FG_ERR_INVALID;
	}

	return service_filegetter_launch(sfg, sockd_out);
}

enum filegetter_code service_filegetter_start_double(service_filegetter_tp sfg,
		service_filegetter_double_args_tp args, int* sockd_out) {
	assert(sfg);
	assert(args);

	memset(sfg, 0, sizeof(service_filegetter_t));

	sfg->type = SFG_DOUBLE;
	sfg->state = SFG_NONE;

	/* if null, we ignore logging */
	sfg->log_cb = args->log_cb;

	/* not required if they only give us addresses. we complain later if needed */
	sfg->hostbyname_cb = args->hostbyname_cb;

	sfg->sleep_cb = args->sleep_cb;
	if(sfg->sleep_cb == NULL) {
		service_filegetter_log(sfg, SFG_CRITICAL, "sleep callback function required");
		return FG_ERR_INVALID;
	}

	/* we download two files, store our specification in current and next */
	sfg->download1 = service_filegetter_get_download_from_args(sfg, &args->http_server, &args->socks_proxy, args->filepath1, args->hostbyname_cb);
	sfg->download2 = service_filegetter_get_download_from_args(sfg, &args->http_server, &args->socks_proxy, args->filepath2, args->hostbyname_cb);

	if(sfg->download1 == NULL || sfg->download2 == NULL) {
		return FG_ERR_INVALID;
	}

	if(strncmp(args->filepath3, "none", 4) == 0) {
		sfg->download3 = NULL;
	} else {
		sfg->download3 = service_filegetter_get_download_from_args(sfg, &args->http_server, &args->socks_proxy, args->filepath3, args->hostbyname_cb);
		if(sfg->download3 == NULL) {
			return FG_ERR_INVALID;
		}
	}

	sfg->current_download = sfg->download1;

	sfg->pausetime_seconds = atoi(args->pausetime_seconds);
	if(sfg->pausetime_seconds < 0) {
		service_filegetter_log(sfg, SFG_WARNING, "silently setting incorrect pausetime to 1 second?");
		sfg->pausetime_seconds = 1;
	}

	return service_filegetter_launch(sfg, sockd_out);
}

static orderedlist_tp service_filegetter_import_download_specs(service_filegetter_tp sfg, service_filegetter_multi_args_tp args) {
	/* reads file with lines of the form:
	 * fileserver.shd:8080:/5mb.urnd
	 */

	if(args->server_specification_filepath == NULL) {
		service_filegetter_log(sfg, SFG_CRITICAL, "please specify a path to a download specification file");
		return NULL;
	}

	FILE* specs = fopen(args->server_specification_filepath, "r");
	if(specs == NULL) {
		service_filegetter_log(sfg, SFG_CRITICAL, "could not open file");
		perror(args->server_specification_filepath);
		return NULL;
	}

	orderedlist_tp ol = orderedlist_create();
	char linebuffer[512];

	while(fgets(linebuffer, sizeof(linebuffer), specs) != NULL) {
		/* strip off the newline */
		if (linebuffer[strlen(linebuffer) - 1] == '\n') {
			linebuffer[strlen(linebuffer) - 1] = '\0';
		}

		orderedlist_tp tokens = orderedlist_create();

		char* result = strtok(linebuffer, ":");
		for(int i = 0; result != NULL; i++) {
			orderedlist_add(tokens, (uint64_t)i, result);
			result = strtok(NULL, ":");
		}

		if(orderedlist_length(tokens) != 3) {
			service_filegetter_log(sfg, SFG_CRITICAL, "format of download specification file incorrect. expected something like \"fileserver.shd:8080:/5mb.urnd\" on each line");
			orderedlist_destroy(tokens, 0);
			orderedlist_destroy(ol, 1);
			return NULL;
		}

		service_filegetter_server_args_t http;
		http.host = orderedlist_remove_first(tokens);
		http.port = orderedlist_remove_first(tokens);
		char* filepath = orderedlist_remove_first(tokens);

		service_filegetter_download_tp dl = service_filegetter_get_download_from_args(sfg, &http, &args->socks_proxy, filepath, args->hostbyname_cb);
		orderedlist_destroy(tokens, 0);

		if(dl == NULL) {
			service_filegetter_log(sfg, SFG_CRITICAL, "error parsing download specification file");
			orderedlist_destroy(ol, 1);
			return NULL;
		} else {
			orderedlist_add(ol, 0, dl);
		}
	}

	fclose(specs);

	/* reorder the keys so they represent indices */
	orderedlist_compact(ol);

	return ol;
}

enum filegetter_code service_filegetter_start_multi(service_filegetter_tp sfg,
		service_filegetter_multi_args_tp args, int* sockd_out) {
	assert(sfg);
	assert(args);

	memset(sfg, 0, sizeof(service_filegetter_t));

	sfg->type = SFG_MULTI;
	sfg->state = SFG_NONE;

	/* if null, we ignore logging */
	sfg->log_cb = args->log_cb;

	/* not required if they only give us addresses. we complain later if needed */
	sfg->hostbyname_cb = args->hostbyname_cb;

	sfg->sleep_cb = args->sleep_cb;
	if(sfg->sleep_cb == NULL) {
		service_filegetter_log(sfg, SFG_CRITICAL, "sleep callback function required");
		return FG_ERR_INVALID;
	}

	if(args->thinktimes_cdf_filepath != NULL) {
		sfg->think_times = cdf_create(args->thinktimes_cdf_filepath);
		if(sfg->think_times == NULL) {
			service_filegetter_log(sfg, SFG_CRITICAL, "problem importing thinktime cdf.");
			return FG_ERR_INVALID;
		}
	}

	sfg->downloads = service_filegetter_import_download_specs(sfg, args);
	if(sfg->downloads == NULL) {
		service_filegetter_log(sfg, SFG_CRITICAL, "problem parsing server download specification file. is the format correct?");
		cdf_destroy(sfg->think_times);
		return FG_ERR_INVALID;
	}

	int runtime_seconds = atoi(args->runtime_seconds);
	if(runtime_seconds > 0) {
		clock_gettime(CLOCK_REALTIME, &sfg->expire);
		sfg->expire.tv_sec += runtime_seconds;
	}

	return service_filegetter_launch(sfg, sockd_out);
}

static enum filegetter_code service_filegetter_expire(service_filegetter_tp sfg) {
	/* all done */
	filegetter_filestats_t total;
	filegetter_stat_aggregate(&sfg->fg, &total);

	/* report aggregate stats */
	service_filegetter_report(sfg, SFG_NOTICE, "[fg-finished]", &total, sfg->downloads_completed, sfg->downloads_requested);

	service_filegetter_stop(sfg);

	return FG_OK_200;
}

enum filegetter_code service_filegetter_activate(service_filegetter_tp sfg, int sockd) {
	assert(sfg);

start_over:

	if((sfg->state == SFG_THINKING || sfg->state == SFG_DOWNLOADING) &&
			sfg->expire.tv_sec > 0) {
		/* they set a service expiration, check if we have expired */
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		if(now.tv_sec > sfg->expire.tv_sec) {
			return service_filegetter_expire(sfg);
		}
	}

	if(sfg->state == SFG_THINKING) {
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		if(now.tv_sec >= sfg->wakeup.tv_sec) {
			/* time to wake up and download the next file */
			service_filegetter_download_next(sfg);
		} else {
			return FG_ERR_WOULDBLOCK;
		}
	}

	if(sfg->state != SFG_DOWNLOADING || sfg->fg.sockd != sockd) {
		return FG_ERR_INVALID;
	}

reactivate:;

	enum filegetter_code result = filegetter_activate(&sfg->fg);

	if(result == FG_ERR_FATAL) {
		/* it had to shut down, lets try again */
		service_filegetter_log(sfg, SFG_NOTICE, "filegetter shutdown due to internal error... restarting");
		filegetter_shutdown(&sfg->fg);
		filegetter_start(&sfg->fg);
		service_filegetter_download_next(sfg);
		goto reactivate;
	}

	/* report progress */
	filegetter_filestats_t stats;
	filegetter_stat_download(&sfg->fg, &stats);

	service_filegetter_report(sfg, SFG_INFO, "[fg-download-progress]", &stats, sfg->downloads_completed+1, sfg->downloads_requested);

	if(result == FG_OK_200) {
		/* completed a download */
		sfg->downloads_completed++;

		sfg->state = SFG_THINKING;

		/* report completion stats */
		service_filegetter_report(sfg, SFG_NOTICE, "[fg-download-complete]", &stats, sfg->downloads_completed, sfg->downloads_requested);

		if(sfg->downloads_completed == sfg->downloads_requested) {
			return service_filegetter_expire(sfg);
		} else {
			if(sfg->type == SFG_MULTI && sfg->think_times != NULL) {
				/* get think time and set wakeup timer */
				unsigned int sleeptime = (unsigned int) (cdf_random_value(sfg->think_times) / 1000);

				clock_gettime(CLOCK_REALTIME, &sfg->wakeup);
				sfg->wakeup.tv_sec += sleeptime;

				/* dont sleep if it would put us beyond our expiration (if its set) */
				if(sfg->expire.tv_sec > 0 && sfg->wakeup.tv_sec > sfg->expire.tv_sec) {
					return service_filegetter_expire(sfg);
				}

				/* call the sleep function, then check if we are done thinking */
				(*sfg->sleep_cb)(sfg, sleeptime);
				goto start_over;
			} else if(sfg->type == SFG_DOUBLE) {
				int time_to_pause = 0;

				if(sfg->current_download == sfg->download1) {
					service_filegetter_log(sfg, SFG_NOTICE, "[fg-double] download1 %lu.%.3d seconds\n", stats.download_time.tv_sec, (int) (stats.download_time.tv_nsec / 1000000));
					sfg->current_download = sfg->download2;
				} else if(sfg->current_download == sfg->download2) {
					service_filegetter_log(sfg, SFG_NOTICE, "[fg-double] download2 %lu.%.3d seconds\n", stats.download_time.tv_sec, (int) (stats.download_time.tv_nsec / 1000000));
					if(sfg->download3 == NULL) {
						time_to_pause = 1;
						sfg->current_download = sfg->download1;
					} else {
						sfg->current_download = sfg->download3;
					}
				} else if(sfg->current_download == sfg->download3) {
					service_filegetter_log(sfg, SFG_NOTICE, "[fg-double] download3 %lu.%.3d seconds\n", stats.download_time.tv_sec, (int) (stats.download_time.tv_nsec / 1000000));
					time_to_pause = 1;
					sfg->current_download = sfg->download1;
				} else {
					service_filegetter_log(sfg, SFG_WARNING, "filegetter download confusion. i dont know what to download next. starting over.");
					sfg->current_download = sfg->download1;
				}

				if(time_to_pause) {
					/* set wakeup timer and call the sleep function  */
					clock_gettime(CLOCK_REALTIME, &sfg->wakeup);
					sfg->wakeup.tv_sec += sfg->pausetime_seconds;
					(*sfg->sleep_cb)(sfg, sfg->pausetime_seconds);
					service_filegetter_log(sfg, SFG_NOTICE, "[fg-pause] pausing for %i seconds\n", sfg->pausetime_seconds);
				} else {
					/* reset download file */
					service_filegetter_download_next(sfg);
					goto reactivate;
				}
			} else {
				/* reset download file */
				service_filegetter_download_next(sfg);
				goto reactivate;
			}
		}
	}

	return result;
}

enum filegetter_code service_filegetter_stop(service_filegetter_tp sfg) {
	assert(sfg);

	service_filegetter_log(sfg, SFG_INFO, "shutting down filegetter");

	enum filegetter_code result = FG_SUCCESS;

	if(sfg->think_times != NULL) {
		cdf_destroy(sfg->think_times);
		sfg->think_times = NULL;
	}

	if(sfg->downloads != NULL) {
		orderedlist_destroy(sfg->downloads, 1);
		sfg->downloads = NULL;
	}

	if(sfg->state != SFG_DONE && sfg->downloads_completed != sfg->downloads_requested) {
		result = filegetter_shutdown(&sfg->fg);
		sfg->current_download = NULL;
		sfg->state = SFG_DONE;
	}

	return result;
}
