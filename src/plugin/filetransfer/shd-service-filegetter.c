/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
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

static void service_filegetter_log(service_filegetter_tp sfg, enum service_filegetter_loglevel level, const gchar* format, ...) {
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

static void service_filegetter_report(service_filegetter_tp sfg, enum service_filegetter_loglevel level, gchar* preamble, filegetter_filestats_tp stats, gint current_download, gint total_downloads) {
	if(preamble != NULL && stats != NULL) {
		GString* reportStringBuffer = g_string_new("");

		g_string_printf(reportStringBuffer, "%s got first bytes in %lu.%.3d seconds and %zu of %zu bytes in %lu.%.3d seconds (download %i",
				preamble,
				stats->first_byte_time.tv_sec, (gint)(stats->first_byte_time.tv_nsec / 1000000),
				stats->body_bytes_downloaded, stats->body_bytes_expected,
				stats->download_time.tv_sec, (gint) (stats->download_time.tv_nsec / 1000000),
				current_download);

		if(total_downloads > 0) {
			g_string_append_printf(reportStringBuffer, " of %i)", total_downloads);
		} else {
			g_string_append_printf(reportStringBuffer, ")");
		}

		service_filegetter_log(sfg, level, "%s", reportStringBuffer->str);
		g_string_free(reportStringBuffer, TRUE);
	}
}

static in_addr_t service_filegetter_getaddr(service_filegetter_tp sfg, service_filegetter_server_args_tp server,
		service_filegetter_hostbyname_cb hostname_cb) {
	/* check if we have an address as a string */
	struct in_addr in;
	gint is_ip_address = inet_aton(server->host, &in);

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
		service_filegetter_server_args_tp socks_proxy, gchar* filepath,
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

	GString* strbuf = g_string_new(http_server->host);
	gint hostlength = strbuf->len;
	gboolean isOnionAddress = g_strstr_len(strbuf->str, strbuf->len, ".onion") ? TRUE : FALSE;
	g_string_free(strbuf, TRUE);

	in_addr_t http_addr = 0;
	if(!isOnionAddress) {
		http_addr = service_filegetter_getaddr(sfg, http_server, hostbyname_cb);
	}
	in_port_t http_port = htons((in_port_t) atoi(http_server->port));
	if((!isOnionAddress && http_addr == 0) || http_port == 0) {
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

	if(isOnionAddress && !socks_addr) {
		service_filegetter_log(sfg, SFG_WARNING, "it probably wont work to specify an .onion address without a Tor socks proxy");
		return NULL;
	}

	/* validation successful */
	service_filegetter_download_tp dl = calloc(1, sizeof(service_filegetter_download_t));
	strncpy(dl->fspec.remote_path, filepath, sizeof(dl->fspec.remote_path));
	strncpy(dl->sspec.http_hostname, http_server->host, sizeof(dl->sspec.http_hostname));
	dl->sspec.http_addr = http_addr;
	dl->sspec.http_port = http_port;
	dl->sspec.socks_addr = socks_addr;
	dl->sspec.socks_port = socks_port;
	dl->sspec.useHostname = isOnionAddress;
	dl->sspec.hostnameLength = hostlength;
	return dl;
}

static enum filegetter_code service_filegetter_download_next(service_filegetter_tp sfg) {
	assert(sfg);

	switch (sfg->type) {

		case SFG_MULTI: {
			/* get a new random download */
			const gint position = (gint) (rand() % g_tree_nnodes(sfg->downloads));

			sfg->current_download = g_tree_lookup(sfg->downloads, &position);

			if(sfg->current_download == NULL) {
				return FG_ERR_INVALID;
			}

			/* follow through to set the download */
		}

		case SFG_SINGLE: {
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

static enum filegetter_code service_filegetter_launch(service_filegetter_tp sfg, gint epolld, gint* sockd_out) {
	/* inputs should be ok, start up the client */
	enum filegetter_code result = filegetter_start(&sfg->fg, epolld);
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
		service_filegetter_single_args_tp args, gint epolld, gint* sockd_out) {
	assert(sfg);
	assert(args);

	memset(sfg, 0, sizeof(service_filegetter_t));

	sfg->type = SFG_SINGLE;
	sfg->state = SFG_NONE;

	/* if null, we ignore logging */
	sfg->log_cb = args->log_cb;
	sfg->hostbyname_cb = args->hostbyname_cb;
	sfg->sleep_cb = args->sleep_cb;

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

	return service_filegetter_launch(sfg, epolld, sockd_out);
}

static gint _treeIntCompare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const gint* ai = a;
	const gint* bi = b;
	g_assert(ai && bi);
	return *ai > *bi ? +1 : *ai == *bi ? 0 : -1;
}

static GTree* service_filegetter_import_download_specs(service_filegetter_tp sfg, service_filegetter_multi_args_tp args) {
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

	gchar linebuffer[512];
	GTree* dlTree = g_tree_new_full(_treeIntCompare, NULL, g_free, g_free);
	gint counter = 0;

	while(fgets(linebuffer, sizeof(linebuffer), specs) != NULL) {
		/* strip off the newline */
		if (linebuffer[strlen(linebuffer) - 1] == '\n') {
			linebuffer[strlen(linebuffer) - 1] = '\0';
		}

		gchar** tokens = g_strsplit((const gchar*) linebuffer, (const gchar*)":", 3);
		if(g_strrstr(tokens[2], ":") != NULL) {
			service_filegetter_log(sfg, SFG_CRITICAL, "format of download specification file incorrect. expected something like \"fileserver.shd:8080:/5mb.urnd\" on each line");
			g_strfreev(tokens);
			g_tree_destroy(dlTree);
			return NULL;
		}

		service_filegetter_server_args_t http;
		http.host = tokens[0];
		http.port = tokens[1];
		gchar* filepath = tokens[2];

		service_filegetter_download_tp dl = service_filegetter_get_download_from_args(sfg, &http, &args->socks_proxy, filepath, args->hostbyname_cb);

		g_strfreev(tokens);

		if(dl == NULL) {
			service_filegetter_log(sfg, SFG_CRITICAL, "error parsing download specification file");
			g_tree_destroy(dlTree);
			return NULL;
		}

		gint* key = g_new(gint, 1);
		*key = counter++;
		g_tree_insert(dlTree, key, dl);
	}

	fclose(specs);

	return dlTree;
}

enum filegetter_code service_filegetter_start_multi(service_filegetter_tp sfg,
		service_filegetter_multi_args_tp args, gint epolld, gint* sockd_out) {
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
		sfg->think_times = cdf_new(0, args->thinktimes_cdf_filepath);
		if(sfg->think_times == NULL) {
			service_filegetter_log(sfg, SFG_CRITICAL, "problem importing thinktime cdf.");
			return FG_ERR_INVALID;
		}
	}

	sfg->downloads = service_filegetter_import_download_specs(sfg, args);
	if(sfg->downloads == NULL) {
		service_filegetter_log(sfg, SFG_CRITICAL, "problem parsing server download specification file. is the format correct?");
		cdf_free(sfg->think_times);
		return FG_ERR_INVALID;
	}

	gint runtime_seconds = atoi(args->runtime_seconds);
	if(runtime_seconds > 0) {
		clock_gettime(CLOCK_REALTIME, &sfg->expire);
		sfg->expire.tv_sec += runtime_seconds;
	}

	if(args->num_downloads) {
		sfg->downloads_requested = atoi(args->num_downloads);
	}

	return service_filegetter_launch(sfg, epolld, sockd_out);
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

enum filegetter_code service_filegetter_activate(service_filegetter_tp sfg, gint sockd) {
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

	if(result == FG_ERR_FATAL || result == FG_ERR_SOCKSCONN) {
		/* it had to shut down */
		service_filegetter_log(sfg, SFG_NOTICE, "filegetter shutdown due to error '%s'... retrying in 60 seconds",
				filegetter_codetoa(result));
		filegetter_shutdown(&sfg->fg);
		filegetter_start(&sfg->fg, sfg->fg.epolld);

		/* set wakeup timer and call the sleep function  */
		sfg->state = SFG_THINKING;
		clock_gettime(CLOCK_REALTIME, &sfg->wakeup);
		sfg->wakeup.tv_sec += 60;
		(*sfg->sleep_cb)(sfg, 60);
		service_filegetter_log(sfg, SFG_NOTICE, "[fg-pause] pausing for 60 seconds");

		return FG_ERR_WOULDBLOCK;
	} else if(result != FG_OK_200 && result != FG_ERR_WOULDBLOCK) {
		service_filegetter_log(sfg, SFG_CRITICAL, "filegetter shutdown due to protocol error '%s'...",
				filegetter_codetoa(result));
		filegetter_shutdown(&sfg->fg);
		return result;
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

		if(sfg->downloads_requested > 0 &&
				sfg->downloads_completed >= sfg->downloads_requested) {
			return service_filegetter_expire(sfg);
		} else {
			if(sfg->type == SFG_MULTI && sfg->think_times != NULL) {
				/* get think time and set wakeup timer */
				gdouble percentile = (gdouble)(((gdouble)rand()) / ((gdouble)RAND_MAX));
				guint sleeptime = (guint) (cdf_getValue(sfg->think_times, percentile) / 1000);

				clock_gettime(CLOCK_REALTIME, &sfg->wakeup);
				sfg->wakeup.tv_sec += sleeptime;

				/* dont sleep if it would put us beyond our expiration (if its set) */
				if(sfg->expire.tv_sec > 0 && sfg->wakeup.tv_sec > sfg->expire.tv_sec) {
					return service_filegetter_expire(sfg);
				}

				/* call the sleep function, then check if we are done thinking */
				(*sfg->sleep_cb)(sfg, sleeptime);
				goto start_over;
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
		cdf_free(sfg->think_times);
		sfg->think_times = NULL;
	}

	if(sfg->downloads != NULL) {
		g_tree_destroy(sfg->downloads);
		sfg->downloads = NULL;
	}

	if(sfg->state != SFG_DONE) {
		result = filegetter_shutdown(&sfg->fg);
		sfg->current_download = NULL;
		sfg->state = SFG_DONE;
	}

	return result;
}
