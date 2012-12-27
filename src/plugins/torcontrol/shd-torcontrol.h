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

#ifndef SHD_TOR_CONTROL_H_
#define SHD_TOR_CONTROL_H_

#include <glib.h>
#include <shd-library.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <event.h>

#define MAX_EVENTS 10
#define TORCTL_NUM_EVENTS 12

enum torctl_code {
	TORCTL_SUCCESS, TORCTL_BLOCK_DOWNLOADED, TORCTL_CLOSED, TORCTL_ERR_INVALID, TORCTL_ERR_FATAL, TORCTL_ERR_BADSD, TORCTL_ERR_WOULDBLOCK, TORCTL_ERR_BUFSPACE,
	TORCTL_ERR_SOCKET, TORCTL_ERR_BIND, TORCTL_ERR_LISTEN, TORCTL_ERR_ACCEPT, TORCTL_ERR_RECV, TORCTL_ERR_SEND, TORCTL_ERR_CLOSE, TORCTL_ERR_EPOLL, TORCTL_ERR_CONNECT,
	TORCTL_ERR_SOCKSINIT, TORCTL_ERR_SOCKSCONN, TORCTL_ERR_NOSERVER,
};

enum torControl_mode {
	TORCTL_MODE_CIRCUITBUILD,
};

enum torControl_events {
	TORCTL_EVENT_NONE = -1,
	TORCTL_EVENT_CIRC = 0,
	TORCTL_EVENT_STREAM = 1,
	TORCTL_EVENT_ORCONN = 2,
	TORCTL_EVENT_BW = 3,
	TORCTL_EVENT_DEBUG = 4,
	TORCTL_EVENT_INFO = 5,
	TORCTL_EVENT_NOTICE = 6,
	TORCTL_EVENT_WARN = 7,
	TORCTL_EVENT_ERR = 8,
	TORCTL_EVENT_NEWDESC = 9,
	TORCTL_EVENT_ADDRMAP = 10,
	TORCTL_EVENT_AUTHDIR_NEWDESCS = 11,
};

typedef struct _TorControl_Args TorControl_Args;
struct _TorControl_Args {
	gchar *mode;
	gint argc;
	gchar **argv;
};

/*
 * CIRC parameter values (status, build flags, purpose and reason)
 */

enum torControl_circStatus {
	TORCTL_CIRC_STATUS_NONE,
	TORCTL_CIRC_STATUS_LAUNCHED,
	TORCTL_CIRC_STATUS_BUILT,
	TORCTL_CIRC_STATUS_EXTENDED,
	TORCTL_CIRC_STATUS_FAILED,
	TORCTL_CIRC_STATUS_CLOSED,
	TORCTL_CIRC_STATUS_UNKNOWN,
};

enum torControl_circBuildFlags {
	TORCTL_CIRC_BUILD_FLAGS_NONE = 0,
	TORCTL_CIRC_BUILD_FLAGS_ONEHOP_TUNNEL = 1<<0,
	TORCTL_CIRC_BUILD_FLAGS_IS_INTERNAL = 1<<1,
	TORCTL_CIRC_BUILD_FLAGS_NEED_CAPACITY = 1<<2,
	TORCTL_CIRC_BUILD_FLAGS_NEED_UPTIME = 1<<3,
	TORCTL_CIRC_BUILD_FLAGS_UNKNOWN = 1<<4,
};

enum torControl_circPurpose {
	TORCTL_CIRC_PURPOSE_NONE,
	TORCTL_CIRC_PURPOSE_GENERAL,
	TORCTL_CIRC_PURPOSE_HS_CLIENT_INTRO,
	TORCTL_CIRC_PURPOSE_HS_CLIENT_REND,
	TORCTL_CIRC_PURPOSE_HS_SERVICE_INTRO,
	TORCTL_CIRC_PURPOSE_HS_SERVICE_REND,
	TORCTL_CIRC_PURPOSE_TESTING,
	TORCTL_CIRC_PURPOSE_CONTROLLER,
	TORCTL_CIRC_PURPOSE_UNKNOWN,
};

enum torControl_circReason {
	TORCTL_CIRC_REASON_NONE,
	TORCTL_CIRC_REASON_TORPROTOCOL,
	TORCTL_CIRC_REASON_INTERNAL,
	TORCTL_CIRC_REASON_REQUESTED,
	TORCTL_CIRC_REASON_HIBERNATING,
	TORCTL_CIRC_REASON_RESOURCELIMIT,
	TORCTL_CIRC_REASON_CONNECTFAILED,
	TORCTL_CIRC_REASON_OR_IDENTITY,
	TORCTL_CIRC_REASON_OR_CONN_CLOSED,
	TORCTL_CIRC_REASON_TIMEOUT,
	TORCTL_CIRC_REASON_FINISHED,
	TORCTL_CIRC_REASON_DESTROYED,
	TORCTL_CIRC_REASON_NOPATH,
	TORCTL_CIRC_REASON_NOSUCHSERVICE,
	TORCTL_CIRC_REASON_MEASUREMENT_EXPIRED,
	TORCTL_CIRC_REASON_UNKNOWN,
};

/*
 * STREAM parameter values (status, purpose, reason)
 */

enum torControl_streamStatus {
	TORCTL_STREAM_STATUS_NONE,
	TORCTL_STREAM_STATUS_NEW,
	TORCTL_STREAM_STATUS_NEWRESOLVE,
	TORCTL_STREAM_STATUS_REMAP,
	TORCTL_STREAM_STATUS_SENTCONNECT,
	TORCTL_STREAM_STATUS_SENTRESOLVE,
	TORCTL_STREAM_STATUS_SUCCEEDED,
	TORCTL_STREAM_STATUS_FAILED,
	TORCTL_STREAM_STATUS_CLOSED,
	TORCTL_STREAM_STATUS_DETATCHED,
	TORCTL_STREAM_STATUS_UNKNOWN,
};

enum torControl_streamReason {
	TORCTL_STREAM_REASON_NONE,
	TORCTL_STREAM_REASON_MISC,
	TORCTL_STREAM_REASON_RESOLVEFAILED,
	TORCTL_STREAM_REASON_CONNECTREFUSED,
	TORCTL_STREAM_REASON_EXITPOLICY,
	TORCTL_STREAM_REASON_DESTROY,
	TORCTL_STREAM_REASON_DONE,
	TORCTL_STREAM_REASON_TIMEOUT,
	TORCTL_STREAM_REASON_NOROUTE,
	TORCTL_STREAM_REASON_HIBERNATING,
	TORCTL_STREAM_REASON_INTERNAL,
	TORCTL_STREAM_REASON_RESOURCELIMIT,
	TORCTL_STREAM_REASON_CONNRESET,
	TORCTL_STREAM_REASON_TORPROTOCOL,
	TORCTL_STREAM_REASON_NOTDIRECTORY,
	TORCTL_STREAM_REASON_END,
	TORCTL_STREAM_REASON_PRIVATE_ADDR,
	TORCTL_STREAM_REASON_UNKNOWN,
};

enum torControl_streamPurpose {
	TORCTL_STREAM_PURPOSE_NONE,
	TORCTL_STREAM_PURPOSE_DIR_FETCH,
	TORCTL_STREAM_PURPOSE_UPLOAD_DESC,
	TORCTL_STREAM_PURPOSE_DNS_REQUEST,
	TORCTL_STREAM_PURPOSE_USER,
	TORCTL_STREAM_PURPOSE_DIRPORT_TEST,
	TORCTL_STREAM_PURPOSE_UNKNOWN,
};

/*
 * ORCONN parameter values (status, reason)
 */

enum torControl_orconnStatus {
	TORCTL_ORCONN_STATUS_NONE,
	TORCTL_ORCONN_STATUS_NEW,
	TORCTL_ORCONN_STATUS_LAUNCHED,
	TORCTL_ORCONN_STATUS_CONNECTED,
	TORCTL_ORCONN_STATUS_FAILED,
	TORCTL_ORCONN_STATUS_CLOSED,
	TORCTL_ORCONN_STATUS_UNKNOWN,
};

enum torControl_orconnReason {
	TORCTL_ORCONN_REASON_NONE,
	TORCTL_ORCONN_REASON_MISC,
	TORCTL_ORCONN_REASON_DONE,
	TORCTL_ORCONN_REASON_CONNECTREFUSED,
	TORCTL_ORCONN_REASON_IDENTITY,
	TORCTL_ORCONN_REASON_CONNECTRESET,
	TORCTL_ORCONN_REASON_TIMEOUT,
	TORCTL_ORCONN_REASON_NOROUTE,
	TORCTL_ORCONN_REASON_IOERROR,
	TORCTL_ORCONN_REASON_RESOURCELIMIT,
	TORCTL_ORCONN_REASON_UNKNOWN,
};

/*
 * LOG parameter values (severity)
 */
enum torControl_logSeverity {
	TORCTL_LOG_SEVERITY_DEBUG,
	TORCTL_LOG_SEVERITY_INFO,
	TORCTL_LOG_SEVERITY_NOTICE,
	TORCTL_LOG_SEVERITY_WARN,
	TORCTL_LOG_SEVERITY_ERR,
	TORCTL_LOG_SEVERITY_UNKNOWN,
};

/* initialize/free function handler */
typedef gboolean (*TorControlInitialize)(gpointer moduleData);
typedef void (*TorControlFree)(gpointer moduleData);

/* event function handlers */
typedef void (*TorControlCircEventFunc)(gpointer moduleData, gint code, gchar* line, gint circID, GString* path, gint status,
        gint buildFlags, gint purpose, gint reason, GDateTime* createTime);
typedef void (*TorControlStreamEventFunc)(gpointer moduleData, gint code, gchar* line, gint streamID, gint circID,
        in_addr_t targetIP, in_port_t targetPort, gint status, gint reason,
        gint remoteReason, gchar *source, in_addr_t sourceIP, in_port_t sourcePort,
        gint purpose);
typedef void (*TorControlORConnEventFunc)(gpointer moduleData, gint code, gchar* line, gint connID, gchar *target, gint status,
        gint reason, gint numCircuits);
typedef void (*TorControlBWEventFunc)(gpointer moduleData, gint code, gchar* line, gint bytesRead, gint bytesWritten);
typedef void (*TorControlExtendedBWEventFunc)(gpointer moduleData, gint code, gchar* line, gchar* type, gint streamID, gint bytesRead, gint bytesWritten);
typedef void (*TorControlCellStatsEventFunc)(gpointer moduleData, gint code, gchar* line, gint circID, gint nextHopCircID, gint prevHopCircID,
		gint appProcessed, gint appTotalWaitMillis, double appMeanQueueLength,
		gint exitProcessed, gint exitTotalWaitMillis, double exitMeanQueueLength);
typedef void (*TorControlTokenEventFunc)(gpointer moduleData, gint code, gchar* line);
typedef void (*TorControlORTokenEventFunc)(gpointer moduleData, gint code, gchar* line);
typedef void (*TorControlLogEventFunc)(gpointer moduleData, gint code, gint severity, gchar *msg);

/* response handler */
typedef void (*TorControlResponseFunc)(gpointer moduleData, GList *reply, gpointer userData);


typedef struct _TorControl_EventHandlers TorControl_EventHandlers;
struct _TorControl_EventHandlers {
    TorControlInitialize initialize;
    TorControlFree free;
	TorControlCircEventFunc circEvent;
	TorControlStreamEventFunc streamEvent;
	TorControlORConnEventFunc orconnEvent;
	TorControlBWEventFunc bwEvent;
	TorControlExtendedBWEventFunc extendedBWEvent;
	TorControlCellStatsEventFunc cellStatsEvent;
	TorControlTokenEventFunc tokenEvent;
	TorControlORTokenEventFunc orTokenEvent;
	TorControlLogEventFunc logEvent;
	TorControlResponseFunc responseEvent;
};

#define TORCTL_CODE_TYPE(code) (code/100)

enum torControl_replyTypes {
	TORCTL_REPLY_SUCCESS = 2,
	TORCTL_REPLY_RETRY = 4,
	TORCTL_REPLY_ERROR = 5,
	TORCTL_REPLY_EVENT = 6,
};

typedef struct _TorControl_ReplyLine TorControl_ReplyLine;
struct _TorControl_ReplyLine {
	gint code;
	gchar *body;
	GList *data;
};

typedef struct _TorControl_ReplyExtended TorControl_ReplyExtended;
struct _TorControl_ReplyExtended {
    gint circID;
};

/* bootstrap status information */
typedef struct _TorControl_BootstrapPhase TorControl_BootstrapPhase;
struct _TorControl_BootstrapPhase {
    gint progress;
    gchar *summary;
};

typedef struct _TorControl_Connection TorControl_Connection;
struct _TorControl_Connection {
	gchar *hostname;
	in_addr_t ip;
	in_port_t port;
	gchar *mode;

	gint sockd;
	gchar buf[16384];
	gsize bufOffset;
	GString *sendBuf;

	GList *reply;
	gchar readingData;

	TorControl_EventHandlers eventHandlers;
	gboolean initialized;

	/* object returned by module init function */
	gpointer moduleData;
};

typedef struct _TorControl TorControl;
struct _TorControl {
	ShadowFunctionTable* shadowlib;
	gint epolld;
	gint sockd;

	GHashTable *connections;
};

void torControl_init(TorControl* currentTorControl);
void torControl_new(TorControl_Args *args);
gint torControl_activate();
void torControl_free();

gint torControl_createConnection(gchar *hostname, in_port_t port, gchar *mode, gchar **args);

gint torControl_authenticate(gint sockd, gchar *password);
gint torControl_setconf(gint sockd, gchar **confValues);
gint torControl_setevents(gint sockd, gchar *events);

gint torControl_buildCircuit(gint sockd, GList *circuit);
gint torControl_attachStream(gint sockd, gint streamID, gint circID);

gint torControl_getInfoBootstrapStatus(gint sockd);

const gchar* torControl_getCircStatusString(enum torControl_circStatus status);
const gchar* torControl_getCircPurposeString(enum torControl_circPurpose purpose);
const gchar* torControl_getCircReasonString(enum torControl_circReason reason);
const gchar* torControl_getStreamStatusString(enum torControl_streamStatus status);
const gchar* torControl_getStreamReasonString(enum torControl_streamReason reason);
const gchar* torControl_getStreamPurposeString(enum torControl_streamPurpose purpose);
const gchar* torControl_getORConnStatusString(enum torControl_orconnStatus status);
const gchar* torControl_getORConnReasonString(enum torControl_orconnReason reason);

#endif /* SHD_TOR_CONTROL_H_ */
