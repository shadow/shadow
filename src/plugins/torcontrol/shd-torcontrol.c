/**
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
#include <glib.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <assert.h>
#include <event.h>

#include <string.h>

#include "shd-torcontrol.h"
#include "shd-torcontrol-circuitbuild.h"
#include "shd-torcontrol-statistics.h"

TorControl* torControl;

#define TORCTL_ASSERTIO(torControl, retcode, allowed_errno_logic, ts_errcode) \
	/* check result */ \
	if(retcode < 0) { \
		/* its ok if we would have blocked or if we are not connected yet, \
		 * just try again later. */ \
		if((allowed_errno_logic)) { \
			return -1; \
		} else { \
			/* some other send error */ \
			fprintf(stderr, "torControl fatal error: %s\n", strerror(errno)); \
			return TORCTL_ERR_FATAL; \
		} \
	} else if(retcode == 0) { \
		/* other side closed */ \
		return TORCTL_ERR_FATAL; \
	}

/*
 * util functions for converting CIRC strings <-> enum
 */

static const gchar* _circuitStatusStrings[] = {
        "NONE", "LAUNCHED", "BUILT", "EXTENDED", "FAILED", "CLOSED", "UNKNOWN",
};
const gchar* torControl_getCircStatusString(enum torControl_circStatus status) {
	return _circuitStatusStrings[status];
}
static gint _torControl_getCircStatus(gchar *str) {
	if(!g_ascii_strcasecmp(str, torControl_getCircStatusString(TORCTL_CIRC_STATUS_LAUNCHED))) {
		return TORCTL_CIRC_STATUS_LAUNCHED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircStatusString(TORCTL_CIRC_STATUS_BUILT))) {
		return TORCTL_CIRC_STATUS_BUILT;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircStatusString(TORCTL_CIRC_STATUS_EXTENDED))) {
		return TORCTL_CIRC_STATUS_EXTENDED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircStatusString(TORCTL_CIRC_STATUS_FAILED))) {
		return TORCTL_CIRC_STATUS_FAILED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircStatusString(TORCTL_CIRC_STATUS_CLOSED))) {
		return TORCTL_CIRC_STATUS_CLOSED;
	}
	return TORCTL_CIRC_STATUS_UNKNOWN;
}

static gint _torControl_getCircBuildFlags(gchar *str) {
	gint ret = TORCTL_CIRC_BUILD_FLAGS_NONE;
	gchar **flags = g_strsplit(str, ",", 0);
	for(gint idx = 0; flags[idx]; idx++) {
		if(!g_ascii_strcasecmp(flags[idx], "ONEHOP_TUNNEL")) {
			ret |= TORCTL_CIRC_BUILD_FLAGS_ONEHOP_TUNNEL;
		} else if(!g_ascii_strcasecmp(flags[idx], "IS_INTERNAL")) {
			ret |= TORCTL_CIRC_BUILD_FLAGS_IS_INTERNAL;
		} else if(!g_ascii_strcasecmp(flags[idx], "NEED_CAPACITY")) {
			ret |= TORCTL_CIRC_BUILD_FLAGS_NEED_CAPACITY;
		} else if(!g_ascii_strcasecmp(flags[idx], "NEED_UPTIME")) {
			ret |= TORCTL_CIRC_BUILD_FLAGS_NEED_UPTIME;
		} else {
			ret |= TORCTL_CIRC_BUILD_FLAGS_UNKNOWN;
		}
	}
	return ret;
}

static const gchar* _circuitPurposeStrings[] = {
        "NONE", "GENERAL", "HS_CLIENT_INTRO", "HS_CLIENT_REND", "HS_SERVICE_INTRO",
        "HS_SERVICE_REND", "TESTING", "CONTROLLER", "UNKNOWN",
};
const gchar* torControl_getCircPurposeString(enum torControl_circPurpose purpose) {
	return _circuitPurposeStrings[purpose];
}
static gint _torControl_getCircPurpose(gchar *str) {
	if(!g_ascii_strcasecmp(str, torControl_getCircPurposeString(TORCTL_CIRC_PURPOSE_GENERAL))) {
		return TORCTL_CIRC_PURPOSE_GENERAL;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircPurposeString(TORCTL_CIRC_PURPOSE_HS_CLIENT_INTRO))) {
		return TORCTL_CIRC_PURPOSE_HS_CLIENT_INTRO;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircPurposeString(TORCTL_CIRC_PURPOSE_HS_CLIENT_REND))) {
		return TORCTL_CIRC_PURPOSE_HS_CLIENT_REND;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircPurposeString(TORCTL_CIRC_PURPOSE_HS_SERVICE_INTRO))) {
		return TORCTL_CIRC_PURPOSE_HS_SERVICE_INTRO;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircPurposeString(TORCTL_CIRC_PURPOSE_HS_SERVICE_REND))) {
		return TORCTL_CIRC_PURPOSE_HS_SERVICE_REND;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircPurposeString(TORCTL_CIRC_PURPOSE_TESTING))) {
		return TORCTL_CIRC_PURPOSE_TESTING;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircPurposeString(TORCTL_CIRC_PURPOSE_CONTROLLER))) {
		return TORCTL_CIRC_PURPOSE_CONTROLLER;
	}
	return TORCTL_CIRC_PURPOSE_UNKNOWN;
}

static const gchar* _circuitReasonStrings[] = {
        "NONE", "TORPROTOCOL", "INTERNAL", "REQUESTED", "HIBERNATING",
        "RESOURCELIMIT", "CONNECTFAILED", "OR_IDENTITY", "OR_CONN_CLOSED",
        "TIMEOUT", "FINISHED", "DESTROYED", "NOPATH", "NOSUCHSERVICE",
        "MEASUREMENT_EXPIRED", "UNKNOWN",
};
const gchar* torControl_getCircReasonString(enum torControl_circReason reason) {
	return _circuitReasonStrings[reason];
}
static gint _torControl_getCircReason(gchar *str) {
	if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_NONE))) {
		return TORCTL_CIRC_REASON_NONE;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_TORPROTOCOL))) {
		return TORCTL_CIRC_REASON_TORPROTOCOL;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_INTERNAL))) {
		return TORCTL_CIRC_REASON_INTERNAL;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_REQUESTED))) {
		return TORCTL_CIRC_REASON_REQUESTED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_HIBERNATING))) {
		return TORCTL_CIRC_REASON_HIBERNATING;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_RESOURCELIMIT))) {
		return TORCTL_CIRC_REASON_RESOURCELIMIT;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_CONNECTFAILED))) {
		return TORCTL_CIRC_REASON_CONNECTFAILED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_OR_IDENTITY))) {
		return TORCTL_CIRC_REASON_OR_IDENTITY;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_OR_CONN_CLOSED))) {
		return TORCTL_CIRC_REASON_OR_CONN_CLOSED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_TIMEOUT))) {
		return TORCTL_CIRC_REASON_TIMEOUT;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_FINISHED))) {
		return TORCTL_CIRC_REASON_FINISHED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_DESTROYED))) {
		return TORCTL_CIRC_REASON_DESTROYED;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_NOPATH))) {
		return TORCTL_CIRC_REASON_NOPATH;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_NOSUCHSERVICE))) {
		return TORCTL_CIRC_REASON_NOSUCHSERVICE;
	} else if(!g_ascii_strcasecmp(str, torControl_getCircReasonString(TORCTL_CIRC_REASON_MEASUREMENT_EXPIRED))) {
		return TORCTL_CIRC_REASON_MEASUREMENT_EXPIRED;
	}
	return TORCTL_CIRC_REASON_UNKNOWN;
}

/*
 * util functions for converting STREAM strings <-> enum
 */

static const gchar* _streamStatusStrings[] = {
        "NONE", "NEW", "NEW_RESOLVE", "REMAP", "SENT_CONNECT", "SENT_RESOLVE",
        "SUCCEECED", "FAILED", "CLOSED", "DETATCHED", "UNKNOWN"
};
const gchar* torControl_getStreamStatusString(enum torControl_streamStatus status) {
	return _streamStatusStrings[status];
}
static gint _torControl_getStreamStatus(gchar *str) {
	if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_NEW))) {
		return TORCTL_STREAM_STATUS_NEW;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_NEWRESOLVE))) {
		return TORCTL_STREAM_STATUS_NEWRESOLVE;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_REMAP))) {
		return TORCTL_STREAM_STATUS_REMAP;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_SENTCONNECT))) {
		return TORCTL_STREAM_STATUS_SENTCONNECT;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_SENTRESOLVE))) {
		return TORCTL_STREAM_STATUS_SENTRESOLVE;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_SUCCEEDED))) {
		return TORCTL_STREAM_STATUS_SUCCEEDED;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_FAILED))) {
		return TORCTL_STREAM_STATUS_FAILED;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_CLOSED))) {
		return TORCTL_STREAM_STATUS_CLOSED;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamStatusString(TORCTL_STREAM_STATUS_DETATCHED))) {
		return TORCTL_STREAM_STATUS_DETATCHED;
	}
	return TORCTL_STREAM_STATUS_UNKNOWN;
}

static const gchar* _streamReasonStrings[] = {
        "NONE", "MISC", "RESOLVEFAILED", "CONNECTREFUSED", "EXITPOLICY", "DESTROY",
        "DONE", "TIMEOUT", "NOROUTE", "HIBERNATING", "INTERNAL", "RESOURCELIMIT",
        "CONNRESET", "TORPROTOCOL", "NOTDIRECTORY", "END", "PRIVATE_ADDR", "UNKNOWN",
};
const gchar* torControl_getStreamReasonString(enum torControl_streamReason reason) {
	return _streamReasonStrings[reason];
}
static gint _torControl_getStreamReason(gchar *str) {
	if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_MISC))) {
		return TORCTL_STREAM_REASON_MISC;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_RESOLVEFAILED))) {
		return TORCTL_STREAM_REASON_RESOLVEFAILED;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_CONNECTREFUSED))) {
		return TORCTL_STREAM_REASON_CONNECTREFUSED;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_EXITPOLICY))) {
		return TORCTL_STREAM_REASON_EXITPOLICY;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_DESTROY))) {
		return TORCTL_STREAM_REASON_DESTROY;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_DONE))) {
		return TORCTL_STREAM_REASON_DONE;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_TIMEOUT))) {
		return TORCTL_STREAM_REASON_TIMEOUT;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_NOROUTE))) {
		return TORCTL_STREAM_REASON_NOROUTE;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_HIBERNATING))) {
		return TORCTL_STREAM_REASON_HIBERNATING;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_INTERNAL))) {
		return TORCTL_STREAM_REASON_INTERNAL;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_RESOURCELIMIT))) {
		return TORCTL_STREAM_REASON_RESOURCELIMIT;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_CONNRESET))) {
		return TORCTL_STREAM_REASON_CONNRESET;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_TORPROTOCOL))) {
		return TORCTL_STREAM_REASON_TORPROTOCOL;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_NOTDIRECTORY))) {
		return TORCTL_STREAM_REASON_NOTDIRECTORY;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_END))) {
		return TORCTL_STREAM_REASON_END;
	} else if(!g_ascii_strcasecmp(str, torControl_getStreamReasonString(TORCTL_STREAM_REASON_PRIVATE_ADDR))) {
		return TORCTL_STREAM_REASON_PRIVATE_ADDR;
	}
	return TORCTL_STREAM_REASON_UNKNOWN;
}

static const gchar* _streamPurposeStrings[] = {
        "NONE", "DIR_FETCH", "UPLOAD_DESC", "DNS_REQUEST", "USER", "DIRPORT_TEST", "UNKNOWN",
};
const gchar* torControl_getStreamPurposeString(enum torControl_streamPurpose purpose) {
	return _streamPurposeStrings[purpose];
}
static gint _torControl_getStreamPurpose(gchar *str) {
	if(!g_ascii_strcasecmp(str, "DIR_FETCH")) {
		return TORCTL_STREAM_PURPOSE_DIR_FETCH;
	} else if(!g_ascii_strcasecmp(str, "UPLOAD_DESC")) {
		return TORCTL_STREAM_PURPOSE_UPLOAD_DESC;
	} else if(!g_ascii_strcasecmp(str, "DNS_REQUEST")) {
		return TORCTL_STREAM_PURPOSE_DNS_REQUEST;
	} else if(!g_ascii_strcasecmp(str, "USER")) {
		return TORCTL_STREAM_PURPOSE_USER;
	} else if(!g_ascii_strcasecmp(str, "DIRPORT_TEST")) {
		return TORCTL_STREAM_PURPOSE_DIRPORT_TEST;
	}
	return TORCTL_STREAM_PURPOSE_UNKNOWN;
}

static const gchar* _orconnStatusStrings[] = {
        "NONE", "NEW", "LAUNCHED", "CONNECTED", "FAILED", "CLOSED", "UNKNOWN",
};
const gchar* torControl_getORConnStatusString(enum torControl_orconnStatus status) {
	return _orconnStatusStrings[status];
}
static gint _torControl_getORConnStatus(gchar *str) {
	if(!g_ascii_strcasecmp(str, torControl_getORConnStatusString(TORCTL_ORCONN_STATUS_NEW))) {
		return TORCTL_ORCONN_STATUS_NEW;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnStatusString(TORCTL_ORCONN_STATUS_LAUNCHED))) {
		return TORCTL_ORCONN_STATUS_LAUNCHED;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnStatusString(TORCTL_ORCONN_STATUS_CONNECTED))) {
		return TORCTL_ORCONN_STATUS_CONNECTED;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnStatusString(TORCTL_ORCONN_STATUS_FAILED))) {
		return TORCTL_ORCONN_STATUS_FAILED;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnStatusString(TORCTL_ORCONN_STATUS_CLOSED))) {
		return TORCTL_ORCONN_STATUS_CLOSED;
	}
	return TORCTL_ORCONN_STATUS_UNKNOWN;
}

static const gchar* _orconnReasonStrings[] = {
        "NONE", "MISC", "DONE", "CONNECTREFUSED", "IDENTITY", "CONNECTRESET",
        "TIMEOUT", "NOROUTE", "IOERROR", "RESOURCELIMIT", "UNKNOWN",
};
const gchar* torControl_getORConnReasonString(enum torControl_orconnReason reason) {
	return _orconnReasonStrings[reason];
}
static gint _torControl_getORConnReason(gchar *str) {
	if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_MISC))) {
		return TORCTL_ORCONN_REASON_MISC;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_DONE))) {
		return TORCTL_ORCONN_REASON_DONE;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_CONNECTREFUSED))) {
		return TORCTL_ORCONN_REASON_CONNECTREFUSED;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_IDENTITY))) {
		return TORCTL_ORCONN_REASON_IDENTITY;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_CONNECTRESET))) {
		return TORCTL_ORCONN_REASON_CONNECTRESET;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_TIMEOUT))) {
		return TORCTL_ORCONN_REASON_TIMEOUT;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_NOROUTE))) {
		return TORCTL_ORCONN_REASON_NOROUTE;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_IOERROR))) {
		return TORCTL_ORCONN_REASON_IOERROR;
	} else if(!g_ascii_strcasecmp(str, torControl_getORConnReasonString(TORCTL_ORCONN_REASON_RESOURCELIMIT))) {
		return TORCTL_ORCONN_REASON_RESOURCELIMIT;
	}
	return TORCTL_ORCONN_REASON_UNKNOWN;
}

/*
 * LOG
 */
static gint _torControl_getLogSeverity(gchar *str) {
	if(!g_ascii_strcasecmp(str, "DEBUG")) {
		return TORCTL_LOG_SEVERITY_DEBUG;
	} else if(!g_ascii_strcasecmp(str, "INFO")) {
		return TORCTL_LOG_SEVERITY_INFO;
	} else if(!g_ascii_strcasecmp(str, "NOTICE")) {
		return TORCTL_LOG_SEVERITY_NOTICE;
	} else if(!g_ascii_strcasecmp(str, "WARN")) {
		return TORCTL_LOG_SEVERITY_WARN;
	} else if(!g_ascii_strcasecmp(str, "ERR")) {
		return TORCTL_LOG_SEVERITY_ERR;
	}
	return TORCTL_LOG_SEVERITY_UNKNOWN;
}

static GDateTime* _torControl_getCreateTime(gchar* datetimestamp) {
	gint year = 0, month = 0, day = 0, hour = 0, min = 0;
	gdouble sec = 0;

	/* parse the stamp format: 1970-01-01T00:07:09.000000 */
	g_assert(datetimestamp);
	gchar** parts = g_strsplit(datetimestamp, "T", 2);
	gchar* date = parts[0];
	gchar* time = parts[1];
	g_assert(date && time);

	gchar** dateparts = g_strsplit(date, "-", 3);
	year = atoi(dateparts[0]);
	month = atoi(dateparts[1]);
	day = atoi(dateparts[2]);

	gchar** timeparts = g_strsplit(time, ":", 3);
	hour = atoi(timeparts[0]);
	min = atoi(timeparts[1]);
	sec = atof(timeparts[2]);

	g_strfreev(dateparts);
	g_strfreev(timeparts);
	g_strfreev(parts);

	GTimeZone* tz = g_time_zone_new_utc();
	GDateTime* dt = g_date_time_new(tz, year, month, day, hour, min, sec);
	g_time_zone_unref(tz);
	return dt;
}

static void _torControl_changeEpoll(gint epolld, gint sockd, gint event) {
    struct epoll_event ev;
    ev.events = event;
    ev.data.fd = sockd;
    epoll_ctl(epolld, EPOLL_CTL_MOD, sockd, &ev);
}

static in_addr_t torControl_resolveHostname(const gchar* hostname) {
	ShadowLogFunc log = torControl->shadowlib->log;
	in_addr_t addr = 0;

	/* get the address in network order */
	if(g_ascii_strncasecmp(hostname, "none", 4) == 0) {
		addr = htonl(INADDR_NONE);
	} else if(g_ascii_strncasecmp(hostname, "localhost", 9) == 0) {
		addr = htonl(INADDR_LOOPBACK);
	} else {
		struct addrinfo* info;
		int ret = getaddrinfo((gchar*) hostname, NULL, NULL, &info);
		if(ret >= 0) {
			addr = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
		} else {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "unable to create client: error in getaddrinfo");
		}
		freeaddrinfo(info);
	}

	return addr;
}

gint torControl_connect(in_addr_t addr, in_port_t port) {
	/* create socket */
	gint sockd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if(sockd < 0) {
		return -1;
	}

	struct sockaddr_in server;
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = addr;
	server.sin_port = port;

	gint result = connect(sockd,(struct sockaddr *) &server, sizeof(server));
	/* nonblocking sockets means inprogress is ok */
	if(result < 0 && errno != EINPROGRESS) {
		return -1;
	}

	/* start watching socket */
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLOUT;
	ev.data.fd = sockd;
	if(epoll_ctl(torControl->epolld, EPOLL_CTL_ADD, sockd, &ev) < 0) {
		return -1;
	}

	return sockd;
}

gint torControl_createConnection(gchar *hostname, in_port_t port, gchar *mode, gchar **args) {
    ShadowLogFunc log = torControl->shadowlib->log;
    TorControl_Connection *connection = g_new0(TorControl_Connection, 1);
    connection->hostname = g_strdup(hostname);
    connection->ip = torControl_resolveHostname(connection->hostname);
    connection->port = port;
    connection->sockd = torControl_connect(connection->ip, htons(connection->port));
    if(connection->sockd < 0) {
        log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error connecting to control host %s:%d",
                connection->hostname, connection->port);
        /* TODO: Free connection object */
        return -1;
    }
    connection->mode = g_strdup(mode);
    if(!g_ascii_strncasecmp(connection->mode, "circuitBuild", 12)) {
        connection->moduleData = torControlCircuitBuild_new(log, connection->sockd, args, &(connection->eventHandlers));
    } else if(!g_ascii_strncasecmp(connection->mode, "statistics", 10)) {
        connection->moduleData = torcontrolstatistics_new(log, hostname, connection->ip, connection->port,
        		connection->sockd, args, &(connection->eventHandlers));
    }
    _torControl_changeEpoll(torControl->epolld, connection->sockd, EPOLLOUT);

    connection->bufOffset = 0;
    connection->reply = NULL;
    connection->readingData = FALSE;
    g_hash_table_insert(torControl->connections, &(connection->sockd), connection);

    return connection->sockd;
}

void torControl_freeReplyLine(gpointer data) {
    TorControl_ReplyLine *replyLine = data;
    g_free(replyLine->body);
    g_list_free_full(replyLine->data, g_free);
    g_free(replyLine);
}

static GString* _torControl_getShortName(gchar* longname) {
	GString* shortName = NULL;
	gchar** nameparts = g_strsplit(longname, "~", 2);
	if(nameparts[1]) {
		shortName = g_string_new(nameparts[1]);
	}
	g_strfreev(nameparts);
	return shortName;
}

static void _torControl_processAsyncCircReply(TorControlCircEventFunc circEvent, gpointer moduleData, gint code, gchar** parts) {
	/* parts[0] is "CIRC" */
	gint circID = atoi(parts[1]);
	gint status = _torControl_getCircStatus(parts[2]);
	gint buildFlags = TORCTL_CIRC_BUILD_FLAGS_NONE;
	gint purpose = TORCTL_CIRC_PURPOSE_NONE;
	gint reason = TORCTL_CIRC_REASON_NONE;
	GDateTime* createTime = NULL;
	GString* path = NULL;

	for(gint idx = 3; parts[idx]; idx++) {
		gchar **param = g_strsplit(parts[idx], "=", 2);
		if(param[0]) {
			if(!g_ascii_strcasecmp(param[0], "BUILD_FLAGS")) {
				buildFlags = _torControl_getCircBuildFlags(param[1]);
			} else if(!g_ascii_strcasecmp(param[0], "PURPOSE")) {
				purpose = _torControl_getCircPurpose(param[1]);
			} else if(!g_ascii_strcasecmp(param[0], "REASON")) {
				reason = _torControl_getCircReason(param[1]);
			} else if(!g_ascii_strcasecmp(param[0], "TIME_CREATED")) {
				createTime = _torControl_getCreateTime(param[1]);
			} else if(!param[1]) {
				/* get the path elements */
				/* $F67E278C346268AB43DE50254D8A8F44FFE486A5~2exit,$9441269F5989487F07AE824063345A0A6BCAB279~4uthority,$F456519D90D678620D591F7465033AE1A4C6582B~1exit */
				gchar** longnames = g_strsplit(param[0], ",", 10);
				for(gint lni = 0; longnames[lni]; lni++) {
					GString* shortName = _torControl_getShortName(longnames[lni]);
					if(shortName) {
						if(path) {
							g_string_append_printf(path, ",%s", shortName->str);
						} else {
							path = g_string_new(shortName->str);
						}
						g_string_free(shortName, TRUE);
					}
				}
				g_strfreev(longnames);
			}
		}
		g_strfreev(param);
	}

	/* notify the module that the event has occured */
	circEvent(moduleData, code, circID, path, status, buildFlags, purpose, reason, createTime);

	/* cleanup */
	if(createTime) {
		g_date_time_unref(createTime);
	}
	if(path) {
		g_string_free(path, TRUE);
	}
}

static void _torControl_processAsyncStreamReply(TorControlStreamEventFunc streamEvent, gpointer moduleData, gint code, gchar** parts) {
	gint streamID = atoi(parts[1]);
	gint status = _torControl_getStreamStatus(parts[2]);
	gint circID = atoi(parts[3]);

	gchar **target = g_strsplit(parts[4], ":", 2);
	in_addr_t targetIP = inet_addr(target[0]);
	in_port_t targetPort = 0;
	if(target[1]) {
		targetPort = atoi(target[1]);
	}
	g_strfreev(target);

	gint reason = TORCTL_STREAM_REASON_NONE;
	gint remoteReason = TORCTL_STREAM_REASON_NONE;
	gchar *source = NULL;
	in_addr_t sourceIP = INADDR_NONE;
	in_port_t sourcePort = 0;
	gint purpose = TORCTL_STREAM_PURPOSE_NONE;

	for(gint idx = 5; parts[idx]; idx++) {
		gchar **param = g_strsplit(parts[idx], "=", 2);
		if(param[0]) {
			if(!g_ascii_strcasecmp(param[0], "REASON")) {
				reason = _torControl_getStreamReason(param[1]);
			} else if(!g_ascii_strcasecmp(param[0], "REMOTE_REASON")) {
				remoteReason = _torControl_getStreamReason(param[1]);
			} else if(!g_ascii_strcasecmp(param[0], "SOURCE")) {
				source = g_strdup(param[1]);
			} else if(!g_ascii_strcasecmp(param[0], "SOURCE_ADDR")) {
				gchar **addr = g_strsplit(param[1], ":", 2);
//                            in_addr_t sourceIP = inet_addr(addr[0]);
//                            in_port_t sourcePort = atoi(addr[1]);
				g_strfreev(addr);
			} else if(!g_ascii_strcasecmp(param[0], "PURPOSE")) {
				purpose = _torControl_getStreamPurpose(param[1]);
			}
		}
		g_strfreev(param);
	}

	streamEvent(moduleData, code, streamID, circID, targetIP, targetPort,
			status, reason, remoteReason, source, sourceIP, sourcePort, purpose);
}

static void _torControl_processAsyncORConnReply(TorControlORConnEventFunc orconnEvent, gpointer moduleData, gint code, gchar** parts) {
    gchar *target = parts[1];
    gint status = _torControl_getORConnStatus(parts[2]);

    gint reason = TORCTL_ORCONN_REASON_NONE;
    gint numCircuits = 0;
    gint connID = 0;
    for(gint idx = 3; parts[idx]; idx++) {
        gchar **param = g_strsplit(parts[idx], "=", 2);
        if(param[0]) {
            if(!g_ascii_strcasecmp(param[0], "REASON")) {
                reason = _torControl_getORConnReason(param[1]);
            } else if(!g_ascii_strcasecmp(param[0], "NCIRCS")) {
                numCircuits = atoi(param[1]);
            } else if(!g_ascii_strcasecmp(param[0], "ID")) {
            	connID = atoi(param[1]);
            }
        }
        g_strfreev(param);
    }

    GString* shortName = _torControl_getShortName(target);
    if(shortName) {
    	target = shortName->str;
    }

    orconnEvent(moduleData, code, connID, target, status, reason, numCircuits);

    if(shortName) {
    	g_string_free(shortName, TRUE);
    }
}

static void _torControl_processAsyncBWReply(TorControlBWEventFunc bwEvent, gpointer moduleData, gint code, gchar** parts) {
    gint bytesRead = atoi(parts[1]);
    gint bytesWritten = atoi(parts[2]);
    bwEvent(moduleData, code, bytesRead, bytesWritten);
}

static void _torControl_processAsyncExtendedBWReply(TorControlExtendedBWEventFunc extendedBWEvent, gchar* type, gpointer moduleData, gint code, gchar** parts) {
    gint id = atoi(parts[1]);
    gint bytesWritten = atoi(parts[2]);
    gint bytesRead = atoi(parts[3]);
	extendedBWEvent(moduleData, type, code, id, bytesRead, bytesWritten);
}

static void _torControl_processAsyncCellStatsReply(TorControlCellStatsEventFunc cellStatsEvent, gpointer moduleData, gint code, gchar** parts) {
	gint circid = atoi(parts[1]);
	gint nextHopCircID = atoi(parts[2]);
	gint appProcessed = atoi(parts[3]);
	gint appTotalWaitMillis = atoi(parts[4]);
	double appMeanQueueLength = atof(parts[5]);
	gint exitProcessed = atoi(parts[6]);
	gint exitTotalWaitMillis = atoi(parts[7]);
	double exitMeanQueueLength = atof(parts[8]);
	cellStatsEvent(moduleData, code, circid, nextHopCircID, appProcessed, appTotalWaitMillis, appMeanQueueLength, exitProcessed, exitTotalWaitMillis, exitMeanQueueLength);
}

static void _torControl_processAsyncLogReply(TorControlLogEventFunc logEvent, gpointer moduleData, gint code, gchar* line) {
    gchar **message = g_strsplit(line, " ", 3);
    gint severity = _torControl_getLogSeverity(message[0]);
    logEvent(moduleData, code, severity, message[2]);
    g_strfreev(message);
}

gint torControl_processReply(TorControl_Connection *connection, GList *reply) {
	ShadowLogFunc log = torControl->shadowlib->log;
	TorControl_ReplyLine *replyLine = g_list_first(reply)->data;

    log(G_LOG_LEVEL_INFO, __FUNCTION__, "[%s] [%d] %s", connection->hostname, replyLine->code, replyLine->body);

    TorControl_EventHandlers *funcs = &(connection->eventHandlers);
    gint code = replyLine->code;
    gchar *line = replyLine->body;

    switch(TORCTL_CODE_TYPE(code)) {
        /* 2XX */
        case TORCTL_REPLY_SUCCESS:
		case TORCTL_REPLY_RETRY:
		case TORCTL_REPLY_ERROR: {
		    TorControlResponseFunc responseFunc = funcs->responseEvent;
		    if(!responseFunc) {
		        break;
		    }

		    for(GList *iter = reply; iter; iter = g_list_next(iter)) {
		        TorControl_ReplyLine *line = iter->data;
		        gpointer userData = NULL;

		        /* check for known responses to part them so we can send a structure
		         * with the data instead of just list of lines */
		        if(g_str_has_prefix(line->body, "EXTENDED")) {
		            /* EXTENDED circID */
		            TorControl_ReplyExtended *extended = g_new0(TorControl_ReplyExtended, 1);
                    gchar **parts = g_strsplit(line->body, " ", 0);
                    extended->circID = atoi(parts[1]);
                    g_strfreev(parts);

                    userData = extended;
		        } else if(g_str_has_prefix(line->body, "status/bootstrap-phase")) {
		            /* status/bootstrap-phase=NOTICE BOOTSTRAP PROGRESS=50 TAG=loading_descriptors SUMMARY="Loading relay descriptors" */
		            TorControl_BootstrapPhase *phase = g_new0(TorControl_BootstrapPhase, 1);

		            gchar **parts = g_strsplit(line->body, " ", 0);
		            for(gint idx = 0; parts[idx]; idx++) {
		                gchar **var = g_strsplit(parts[idx], "=", 2);
		                if(!g_strcmp0(var[0], "PROGRESS")) {
		                    phase->progress = g_ascii_strtoll(var[1], NULL, 10);
		                } else if(!g_strcmp0(var[0], "SUMMARY")) {
		                    phase->summary = g_strdup(var[1]);
		                }
		                g_strfreev(var);
		            }
		            g_strfreev(parts);

		            userData = phase;
		        }

		        responseFunc(connection->moduleData, reply, userData);
		        if(userData) {
		            g_free(userData);
		        }
		    }

			break;
		}

        /* 6XX */
        case TORCTL_REPLY_EVENT: {
            gchar **parts = g_strsplit(line, " ", 0);
            gchar *event = parts[0];

            if(funcs->circEvent && !g_ascii_strcasecmp(event, "CIRC")) {
                _torControl_processAsyncCircReply(funcs->circEvent, connection->moduleData, code, parts);
            } else if(funcs->streamEvent && !g_ascii_strcasecmp(event, "STREAM")) {
                _torControl_processAsyncStreamReply(funcs->streamEvent, connection->moduleData, code, parts);
            } else if(funcs->orconnEvent && !g_ascii_strcasecmp(event, "ORCONN")) {
            	_torControl_processAsyncORConnReply(funcs->orconnEvent, connection->moduleData, code, parts);
            } else if(funcs->bwEvent && !g_ascii_strcasecmp(event, "BW")) {
                _torControl_processAsyncBWReply(funcs->bwEvent, connection->moduleData, code, parts);
            } else if(funcs->extendedBWEvent &&
            		(!g_ascii_strcasecmp(event, "STREAM_BW") ||
            		!g_ascii_strcasecmp(event, "ORCONN_BW") ||
            		!g_ascii_strcasecmp(event, "DIRCONN_BW") ||
            		!g_ascii_strcasecmp(event, "EXITCONN_BW"))) {
            	_torControl_processAsyncExtendedBWReply(funcs->extendedBWEvent, event, connection->moduleData, code, parts);
            } else if (funcs->cellStatsEvent && !g_ascii_strcasecmp(event, "CELL_STATS")) {
            	_torControl_processAsyncCellStatsReply(funcs->cellStatsEvent, connection->moduleData, code, parts);
            }else if(funcs->logEvent && (!g_ascii_strcasecmp(event, "DEBUG") ||
                    !g_ascii_strcasecmp(event, "INFO") || !g_ascii_strcasecmp(event, "NOTICE") ||
                    !g_ascii_strcasecmp(event, "WARN") || !g_ascii_strcasecmp(event, "ERR"))) {
            	_torControl_processAsyncLogReply(funcs->logEvent, connection->moduleData, code, line);
            }

            g_strfreev(parts);
            break;
        }
    }

	return 0;
}

/*
 * functions to send commands to the tor controller
 */

gint torControl_sendCommand(gint sockd, gchar *command) {
    ShadowLogFunc log = torControl->shadowlib->log;
    TorControl_Connection *connection = g_hash_table_lookup(torControl->connections, &sockd);

    /* all commands must end with CRLF */
    GString *buf = g_string_new("");
    g_string_printf(buf, "%s\r\n", command);

    _torControl_changeEpoll(torControl->epolld, sockd, EPOLLOUT);
    gint bytes = send(sockd, buf->str, buf->len, 0);
    g_string_free(buf, TRUE);
    TORCTL_ASSERTIO(torControl, bytes, errno == EWOULDBLOCK || errno == ENOTCONN || errno == EALREADY, TORCTL_ERR_SEND);

    log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "[%s] CMD: %s", connection->hostname, command);
    if(bytes >= buf->len) {
        _torControl_changeEpoll(torControl->epolld, sockd, EPOLLIN);
    }

    return bytes;
}

gint torControl_authenticate(gint sockd, gchar *password) {
    GString *command = g_string_new("");
    g_string_printf(command, "AUTHENTICATE \"%s\"", password);

    gint bytes = torControl_sendCommand(sockd, command->str);
    g_string_free(command, TRUE);

    return bytes;
}

gint torControl_setconf(gint sockd, gchar **confValues) {
    GString *command = g_string_new("");
    g_string_printf(command, "SETCONF");

    gint idx = 0;
    while(confValues[idx]) {
        g_string_append_printf(command, " %s=%s", confValues[idx], confValues[idx + 1]);
        idx += 2;
    }

    gint bytes = torControl_sendCommand(sockd, command->str);
    g_string_free(command, TRUE);

    return bytes;
}

gint torControl_setevents(gint sockd, gchar *events) {
    GString *command = g_string_new("");
    g_string_printf(command, "SETEVENTS EXTENDED %s", events);

    gint bytes = torControl_sendCommand(sockd, command->str);
    g_string_free(command, TRUE);

    return bytes;
}

gint torControl_buildCircuit(gint sockd, GList *circuit) {
    ShadowLogFunc log = torControl->shadowlib->log;
    if(g_list_length(circuit) == 0) {
        log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Cannot create circuit of length 0");
        return -1;
    }

    GString *command = g_string_new("");
    g_string_printf(command, "EXTENDCIRCUIT 0 %s", (gchar *)(g_list_first(circuit)->data));
    for(GList *iter = g_list_next(g_list_first(circuit)); iter; iter = g_list_next(iter)) {
        g_string_append_printf(command, ",%s", (gchar *)iter->data);
    }

    gint bytes = torControl_sendCommand(sockd, command->str);
    g_string_free(command, TRUE);

    return bytes;
}

gint torControl_attachStream(gint sockd, gint streamID, gint circID) {
    GString *command = g_string_new("");
    g_string_append_printf(command, "ATTACHSTREAM %d %d", streamID, circID);

    gint bytes = torControl_sendCommand(sockd, command->str);
    g_string_free(command, TRUE);

    return bytes;
}

gint torControl_getInfoBootstrapStatus(gint sockd) {
    GString *command = g_string_new("GETINFO status/bootstrap-phase");

    gint bytes = torControl_sendCommand(sockd, command->str);
    g_string_free(command, TRUE);

    return bytes;
}

/*
 * Tor control plugin functions
 */

void torControl_init(TorControl* currentTorControl) {
	torControl = currentTorControl;
}

void torControl_new(TorControl_Args *args) {
	ShadowLogFunc log = torControl->shadowlib->log;
	log(G_LOG_LEVEL_DEBUG, __FUNCTION__, "torControl_new called");

	/* create an epoll to wait for I/O events */
	torControl->epolld = epoll_create(1);
	if(torControl->epolld == -1) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error in epoll_create");
		close(torControl->epolld);
		torControl->epolld = 0;
		return;
	}

	/* read in filename with hosts to connect to */
	gchar *contents;
	gsize length;
	GError *error;

	if(!g_file_get_contents(args->hostsFilename, &contents, &length, &error)) {
		/* TODO: HANDLE ERROR */
	}

	torControl->connections = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, NULL);

	gchar **lines = g_strsplit(contents, "\n", 0);
	for(gint lineNum = 0; lines[lineNum]; lineNum++) {
		if(strlen(lines[lineNum]) == 0) {
			continue;
		}
		log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "%s", lines[lineNum]);
		/* hostname:port mode [args] */
		gchar **parts = g_strsplit_set(lines[lineNum], " :", 4);
		gchar *hostname = parts[0];
		in_port_t port = atoi(parts[1]);
		gchar *mode = parts[2];
		gchar **args = g_strsplit(parts[3], " ", 0);

		gint ret = torControl_createConnection(hostname, port, mode, args);
		if(ret < 0) {
		    log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "Error creating connection to %s:%d for %s", hostname, port, mode);
		}

		g_strfreev(args);
		g_strfreev(parts);
	}
	g_strfreev(lines);
}

gint torControl_activate() {
	ShadowLogFunc log = torControl->shadowlib->log;

	struct epoll_event events[10];
	int nfds = epoll_wait(torControl->epolld, events, 10, 0);
	if(nfds == -1) {
		log(G_LOG_LEVEL_WARNING, __FUNCTION__, "error in epoll_wait");
		return -1;
	}

	gint bytes;
	for(int i = 0; i < nfds; i++) {
		gint sockd = events[i].data.fd;
		TorControl_Connection *connection = g_hash_table_lookup(torControl->connections, &sockd);
		if(!connection) {
			log(G_LOG_LEVEL_WARNING, __FUNCTION__, "Error: could not find sockd %d", sockd);
			continue;
		}

		/* keep calling initialize as needed */
		if(!connection->initialized) {
		    connection->initialized = connection->eventHandlers.initialize(connection->moduleData);
		}

		if(events[i].events & EPOLLIN) {
            gsize space = sizeof(connection->buf) - connection->bufOffset;
            gchar *recvpos = connection->buf + connection->bufOffset;
            bytes = recv(sockd, recvpos, space, 0);
            TORCTL_ASSERTIO(torControl, bytes, errno == EWOULDBLOCK, TORCTL_ERR_RECV);
            recvpos[bytes] = 0;

            gchar **lines = g_strsplit(connection->buf, "\r\n", 0);
            gint idx;
            /* go through all the reply lines and build a completely response */
            for(idx = 0; lines[idx] && lines[idx + 1]; idx++) {
                if(!connection->readingData) {
                    TorControl_ReplyLine *replyLine = g_new0(TorControl_ReplyLine, 1);
                    replyLine->code = g_ascii_strtoll(lines[idx], NULL, 10);
                    replyLine->body = g_strdup(lines[idx] + 4);
                    replyLine->data = NULL;

                    /* check to see if this is the first line in the reply */
                    connection->reply = g_list_append(connection->reply, replyLine);

                    /* if + separates the code and message, this means that the data
                     * is split into multiple lines, so keep on reading lines as data */
                    if(lines[idx][3] == '+') {
                        connection->readingData = TRUE;
                    } else if(lines[idx][3] == ' ') {
                        torControl_processReply(connection, connection->reply);
                        g_list_free_full(connection->reply, torControl_freeReplyLine);
                        connection->reply = NULL;
                    }
                } else {
                    TorControl_ReplyLine *replyLine = g_list_last(connection->reply)->data;
                    if(!g_ascii_strcasecmp(lines[idx], ".\r\n") || !g_ascii_strcasecmp(lines[idx], "650 OK\r\n") ||
                       !g_ascii_strcasecmp(lines[idx], ".\n") || !g_ascii_strcasecmp(lines[idx], "650 OK\n")) {
                        connection->readingData = FALSE;

                        /* if code is 6XX, this is an event and can be processed;
                         * else need to receive 250 OK first */
                        if(replyLine->code / 600) {
                            torControl_processReply(connection, connection->reply);
                            g_list_free_full(connection->reply, torControl_freeReplyLine);
                            connection->reply = NULL;
                        }
                    } else {
                        replyLine->data = g_list_append(replyLine->data, g_strdup(lines[idx]));
                    }
                }
            }

            /* if there is any part of the reply left, buffer it for next time */
            if(lines[idx] && strlen(lines[idx]) > 0) {
                strcpy(connection->buf, lines[idx]);
                connection->bufOffset = strlen(lines[idx]);
            } else {
            	connection->bufOffset = 0;
            }

            g_strfreev(lines);
		}
	}

	return 0;
}

void torControl_free() {
    /* TODO: free connections and other allocated memory */
	/* TODO: call the handlers->free function for each connection module */
}
