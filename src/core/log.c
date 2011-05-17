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

/* for asprintf */
#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>

#include "global.h"
#include "log.h"
#include "log_codes.h"
#include "netconst.h"
#include "routing.h"
#include "context.h"
#include "sim.h"
#include "resolver.h"

static struct {
	enum shadow_log_code max_level;
	char prefix[100];

	int use_dvn_routing ;

	logger_t channels[LOG_NUM_CHANNELS];
} _log_system ;

static enum shadow_log_code dlog_loglvl_to_int(char* loglevel) {
	if(strcasecmp(loglevel, "error") == 0) {
		return LOG_ERR;
	} else if(strcasecmp(loglevel, "critical") == 0) {
		return LOG_CRIT;
	} else if(strcasecmp(loglevel, "warning") == 0) {
		return LOG_WARN;
	} else if(strcasecmp(loglevel, "message") == 0) {
		return LOG_MSG;
	} else if(strcasecmp(loglevel, "info") == 0) {
		return LOG_INFO;
	} else if(strcasecmp(loglevel, "debug") == 0) {
		return LOG_DEBUG;
	} else {
		return LOG_MSG;
	}
}

void dlog_init(char* loglevel) {
	_log_system.max_level = dlog_loglvl_to_int(loglevel);
	_log_system.use_dvn_routing = 0;

	strcpy(_log_system.prefix, "");

	memset(_log_system.channels, 0, sizeof(_log_system.channels));
	_log_system.channels[0].type = LOGGER_TYPE_STDOUT;

	return;
}

void dlog_cleanup(void) {
	for (int i=0; i<LOG_NUM_CHANNELS; i++)
		dlog_close_channel(i);
}

void dlog_set_dvn_routing(int enabled) {
	if(enabled) {
		/* close all open channels */
		dlog_cleanup();

		/* will use dvn_packet_route() for all logging operations */
		_log_system.use_dvn_routing = 1;
	} else
		_log_system.use_dvn_routing = 0;
}

void dlog_close_channel(int channel) {
	logger_tp curl;
	if(channel < 0 || channel >= LOG_NUM_CHANNELS)
		return;
	curl=&_log_system.channels[channel];

	switch(curl->type) {
	case LOGGER_TYPE_SOCKET:
		if(socket_isvalid(curl->detail.tcpsocket.sock)) {
		//	dvn_drop_socket(curl->detail.tcpsocket.sock);
			socket_close(curl->detail.tcpsocket.sock);
		}
		break;
	case LOGGER_TYPE_FILE:
		fclose(curl->detail.file.file);
		break;
	}

	curl->type = LOGGER_TYPE_NULL;
	return;
}

void dlog_set_channel(int channel, char * destination, int process_identifier) {
	char buffer[256];
	logger_tp curl;

	if(channel < 0 || channel >= LOG_NUM_CHANNELS)
		return;
	curl=&_log_system.channels[channel];

	/* close any existing open files/sockets */
	dlog_close_channel(channel);

	if(channel == 0)
		curl->type = LOGGER_TYPE_STDOUT;

	/* figure out what the new one is! */
	if(!strcmp(destination, "stdout")) {
		curl->type = LOGGER_TYPE_STDOUT;
		dlogf(LOG_MSG, "Logs: Connected to STDOUT on log channel %d.\n", channel);

	} else if(strstr(destination, "file:") == destination) {
		FILE * file;
		char * destfile;
		size_t destfile_size;

		destfile = destination + 5;
		destfile_size = strlen(destfile);
		if(destfile_size == 0)
			return;

		if(destfile_size > sizeof(buffer) - 10)
			destfile[sizeof(buffer) - 10] = 0;
		sprintf(&destfile[destfile_size], ".%i", process_identifier);

		file = fopen(destfile, "a");
		if(!file) {
			dlogf(LOG_ERR, "Logs: Unable to open file '%s' on log channel %d.\n", destfile, channel);
			return;
		}

		curl->detail.file.file = file;
		strncpy(curl->detail.file.path, destfile, sizeof(curl->detail.file.path));
		curl->detail.file.path[sizeof(curl->detail.file.path)-1]=0;
		curl->type = LOGGER_TYPE_FILE;

		dlogf(LOG_MSG, "Logs: Opened file '%s' on log channel %d.\n", destfile, channel);

	} else if(strstr(destination, "socket:") == destination) {
		socket_tp newsocket;
		char * host, * portstr;
		int port;

		host = destination + 7;
		portstr = strchr(host, ':');

		if(strlen(host) == 0 || !portstr || strlen(portstr) < 2)
			return;

		*(portstr++) = 0;
		port = atoi(portstr);
		if(port == 0)
			return;

		newsocket= socket_create(SOCKET_OPTION_TCP | SOCKET_OPTION_NONBLOCK);
		if(!socket_connect(newsocket, host, port)) {
			dlogf(LOG_ERR, "Logs: Unable to connect to '%s:%d' on log channel %d.\n", host, port, channel);
			socket_close(newsocket);
			return;
		}

		//dvn_watch_socket(newsocket);
		curl->detail.tcpsocket.sock = newsocket;
		strncpy(curl->detail.tcpsocket.host, host, sizeof(curl->detail.tcpsocket.host));
		curl->detail.tcpsocket.host[sizeof(curl->detail.tcpsocket.host)-1] = 0;
		curl->detail.tcpsocket.port = port;
		curl->type = LOGGER_TYPE_SOCKET;

		dlogf(LOG_MSG, "Logs: Connected to '%s:%d' on log channel %d.\n", host, port, channel);
	}
}

void dlog_update_status(void) {
	for (int i=0; i<LOG_NUM_CHANNELS; i++) {
		if(_log_system.channels[i].type == LOGGER_TYPE_SOCKET && !socket_isvalid(_log_system.channels[i].detail.tcpsocket.sock)) {
			//dvn_drop_socket(_log_system.channels[i].detail.tcpsocket.sock);
			socket_destroy(_log_system.channels[i].detail.tcpsocket.sock);
			_log_system.channels[i].type = LOGGER_TYPE_STDOUT;
			dlogf(LOG_MSG, "Logs: Log channel %d was disconnected from '%s:%d'.\n", i,_log_system.channels[i].detail.tcpsocket.host, _log_system.channels[i].detail.tcpsocket.port);
		}
	}
}


void dlog_deposit(int frametype, nbdf_tp frame) {
	unsigned int channel, data_length;
	char * data;

	if(frametype != DVN_FRAME_LOG)
		return;

	nbdf_read(frame, "iB", &channel, &data_length, &data);

	if(data_length && data) {
		dlog_channel_write(channel, data, data_length);
		free(data);
	}

	return;
}

void dlog_channel_write(int channel, char * data, unsigned int length) {
	logger_tp curl;

	if(channel < 0 || channel >= LOG_NUM_CHANNELS || !data)
		return;

	if(_log_system.use_dvn_routing) {
		/* dvn routing mode */
		nbdf_tp log_frame = nbdf_construct("ib", channel, length, data);
		dvn_packet_route(DVNPACKET_LOG, DVNPACKET_LAYER_PRC, 0, DVN_FRAME_LOG, log_frame);
		nbdf_free(log_frame);
		return;
	}

	curl=&_log_system.channels[channel];

	switch(curl->type) {
		case LOGGER_TYPE_NULL:
			break;

		case LOGGER_TYPE_STDOUT: {
			char temp[length+1];
			memcpy(temp, data, length);
			temp[length] = 0;
			printf("%s", temp);
#ifdef DEBUG
			fflush(stdout);
#endif
			break;
		}

		case LOGGER_TYPE_SOCKET:
			if(socket_isvalid(curl->detail.tcpsocket.sock))
				socket_write(curl->detail.tcpsocket.sock, data, length);
			break;

		case LOGGER_TYPE_FILE:
			fwrite(data, 1, length, curl->detail.file.file);
			break;
	}
	return;
}

void dlog_setprefix(char * pre) {
	strcpy(_log_system.prefix, pre);
}

void dlogf_bin(char * d, int length) {
#ifdef DEBUG
	unsigned int i ;
	for(i=0; i<length; i++) {
		if(i%10 == 0)
			printf("\n");
		printf("%1.2hhx %c ", d[i], isprint(d[i])?d[i]:'.');
	}
	printf("\n");
#endif
}

void dlogf(enum shadow_log_code level, char *fmt, ...) {
	va_list vargs;
	va_start(vargs, fmt);
	dlogf_main(level, CONTEXT_SHADOW, fmt, vargs);
	va_end(vargs);
}

void dlogf_main(enum shadow_log_code level, enum shadow_log_context context, char *fmt, va_list vargs) {
	static char buffer1[2048], buffer2[2048];
	char * lvltxt;
	unsigned int len;
	va_list vargs_copy;
	char* status_prefix;

	if( level > _log_system.max_level )
		return;

#ifndef DEBUG
	if(level == LOG_DEBUG)
		return;
#endif

	switch(level) {
		case LOG_ERR:
			lvltxt = "ERROR:";
		break;
		case LOG_CRIT:
			lvltxt = "CRITICAL:";
		break;
		case LOG_WARN:
			lvltxt = "WARNING:";
		break;
		case LOG_MSG:
			lvltxt = "MESSAGE:";
		break;
		default:
		case LOG_INFO:
			lvltxt = "INFO:";
		break;
		case LOG_DEBUG:
			lvltxt = "DEBUG:";
		break;
	}

	va_copy(vargs_copy, vargs);
	vsnprintf(buffer2, sizeof(buffer2), fmt, vargs);
	va_end(vargs_copy);

	switch (context) {
		case CONTEXT_SHADOW:
			status_prefix = dlog_get_status_prefix("shadow");
			break;
		case CONTEXT_MODULE:
			status_prefix = dlog_get_status_prefix("module");
			break;
		default:
			status_prefix = dlog_get_status_prefix("unknown");
			break;
	}

	int free_stat = 1;
	if(status_prefix == NULL) {
		status_prefix = "shadow";
		free_stat = 0;
	}

	/* dont overflow buffer1 */
	len = snprintf(buffer1, sizeof(buffer1), "%s%s%s %s",
			status_prefix, _log_system.prefix, lvltxt, buffer2);

	if(len >= sizeof(buffer1)) {
		/* message was truncated */
		char* truncmsg = "[truncated...]\n";
		char* writeposition = buffer1 + sizeof(buffer1) - strlen(truncmsg) - 1;
		snprintf(writeposition, strlen(truncmsg) + 1, "%s", truncmsg);
	} else {
		/* include the null byte when writing to channel */
		len++;
	}

	dlog_channel_write(0, buffer1, len);

	if(free_stat) {
		free(status_prefix);
	}
}

char* dlog_get_status_prefix(char* caller_str) {
	ptime_t simtime = 0;
	unsigned int sid = 0, wid = 0;
	char* status_str;
	int len;

	char* name = "";
	in_addr_t addr = 0;
	char addr_string[INET_ADDRSTRLEN+1];
	memset(addr_string, 0, sizeof(addr_string));

	/* we prefer an address from vci, and go to the current context as backup */
	if(global_sim_context.sim_worker != NULL) {
		simtime = global_sim_context.sim_worker->current_time;
		if(global_sim_context.sim_worker->vci_mgr != NULL) {
			sid = global_sim_context.sim_worker->vci_mgr->slave_id;
			wid = global_sim_context.sim_worker->vci_mgr->worker_id;
			if(global_sim_context.sim_worker->vci_mgr->current_vsocket_mgr != NULL) {
				addr = global_sim_context.sim_worker->vci_mgr->current_vsocket_mgr->addr;
			} else if(global_sim_context.current_context != NULL) {
				if(global_sim_context.current_context->vsocket_mgr != NULL) {
					addr = global_sim_context.current_context->vsocket_mgr->addr;
				}
			}
		}
		if(addr != 0) {
			name = resolver_resolve_byaddr(global_sim_context.sim_worker->resolver, addr);
		}
	}

	inet_ntop(AF_INET, &addr, addr_string, INET_ADDRSTRLEN);

	len = asprintf(&status_str, "|t=%lu.%.3d|s=%u|w=%u|%s|%s|%s| ",
			(unsigned long)simtime/1000, (int)simtime%1000, sid, wid, caller_str, addr_string, name);
	if(len < 0) {
		return NULL;
	} else {
		return status_str;
	}
}
