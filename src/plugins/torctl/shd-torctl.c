/*
 * See LICENSE for licensing information
 */

#include "shd-torctl.h"

#define MAGIC 0xFFEEDDCC

/* if option is specified, run as client, else run as server */
static const gchar* USAGE = "USAGE: torctl hostname port event1,event2,...,eventN\n";

typedef enum _TorCTLState TorCTLState;
enum _TorCTLState {
	TCS_NONE, TCS_AUTHENTICATING, TCS_BOOTSTRAPPING, TCS_LOGGING,
};

/* all state for torctl is stored here */
struct _TorCTL {
	/* the function we use to log messages
	 * needs level, functionname, and format */
	ShadowLogFunc slogf;

	/* the epoll descriptor to which we will add our sockets.
	 * we use this descriptor with epoll to watch events on our sockets. */
	gint ed;
	/* the socked descriptor for the tor control connection */
	gint sd;

	/* the state of our connection with Tor */
	TorCTLState state;
	/* our client got a response and we can exit */
	gboolean isDone;
	/* we have the CLIENT_STATUS event set, waiting for bootstrapping */
	gboolean isStatusEventSet;

	GString* hostname;
	in_addr_t netip; /* stored in network order */
	in_port_t netport; /* stored in network order */

	GQueue* commands;
	GString* receiveLineBuffer;
	GString* eventsCommand;

	guint magic;
};

static gint _torctl_parseCode(gchar* line) {
	g_assert(line);
	gchar** parts1 = g_strsplit(line, " ", 0);
	gchar** parts2 = g_strsplit(parts1[0], "-", 0);
	gint code = atoi(parts2[0]);
	g_strfreev(parts1);
	g_strfreev(parts2);
	return code;
}

static gint _torctl_parseBootstrapProgress(gchar* line) {
	g_assert(line);
	gint progress = -1;
	gchar** parts = g_strsplit(line, " ", 0);
	gchar* part = NULL;
	gboolean foundBootstrap = FALSE;
	for(gint j = 0; (part = parts[j]) != NULL; j++) {
		gchar** subparts = g_strsplit(part, "=", 0);
		if(!g_ascii_strncasecmp(subparts[0], "BOOTSTRAP", 9)) {
			foundBootstrap = TRUE;
		} else if(foundBootstrap && !g_ascii_strncasecmp(subparts[0], "PROGRESS", 8)) {
			progress = atoi(subparts[1]);
		}
		g_strfreev(subparts);
	}
	g_strfreev(parts);
	return progress;
}

static void _torctl_epoll(TorCTL* torctl, gint operation, guint32 events) {
	g_assert(torctl && (torctl->magic == MAGIC));

	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	ev.events = events;
	ev.data.fd = torctl->sd;

	gint res = epoll_ctl(torctl->ed, operation, torctl->sd, &ev);
	if(res == -1) {
		torctl->slogf(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__, "error in epoll_ctl");
	}
}

static void _torctl_processLine(TorCTL* torctl, GString* linebuf) {
	g_assert(torctl && (torctl->magic == MAGIC));
	g_assert(linebuf);

	switch(torctl->state) {

		case TCS_NONE: {
			break;
		}

		case TCS_AUTHENTICATING: {
			gint code = _torctl_parseCode(linebuf->str);
			if(code == 250) {
				torctl->slogf(SHADOW_LOG_LEVEL_INFO, __FUNCTION__,
							"successfully received auth response '%s'", linebuf->str);
				g_queue_push_tail(torctl->commands, g_string_new("GETINFO status/bootstrap-phase\r\n"));
				torctl->state = TCS_BOOTSTRAPPING;
			} else {
				torctl->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
							"received failed auth response '%s'", linebuf->str);
			}
			break;
		}

		case TCS_BOOTSTRAPPING: {
			/* we will be getting all client status events, not all of them have bootstrap status */
			gint progress = _torctl_parseBootstrapProgress(linebuf->str);
			if(progress >= 0) {
				torctl->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__,
							"successfully received bootstrap phase response '%s'", linebuf->str);
				if(progress >= 100) {
					torctl->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
							"torctl ready (Bootstrapped 100)");

					g_queue_push_tail(torctl->commands, g_string_new(torctl->eventsCommand->str));

					torctl->isStatusEventSet = FALSE;
					torctl->state = TCS_LOGGING;
				} else if(!(torctl->isStatusEventSet)) {
					/* not yet at 100%, register the async status event to wait for it */
					g_queue_push_tail(torctl->commands, g_string_new("SETEVENTS EXTENDED STATUS_CLIENT\r\n"));
					torctl->isStatusEventSet = TRUE;
				}
			}
			break;
		}

		case TCS_LOGGING: {
			torctl->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"[torctl-log] %s:%u %s", torctl->hostname->str, ntohs(torctl->netport), linebuf->str);
			break;
		}

		default:
			/* this should never happen */
			torctl->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
					"reached unreachable default state, exiting");
			g_assert(FALSE);
			break;
	}
}

static void _torctl_activate(TorCTL* torctl, uint32_t events) {
	g_assert(torctl && (torctl->magic == MAGIC));

	/* bootstrap */
	if(torctl->state == TCS_NONE && (events & EPOLLOUT)) {
		/* our control socket is connected */
		g_queue_push_tail(torctl->commands, g_string_new("AUTHENTICATE\r\n"));
		torctl->state = TCS_AUTHENTICATING;
	}

	/* send all queued commands */
	if(events & EPOLLOUT) {
		torctl->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "EPOLLOUT is set");

		while(!g_queue_is_empty(torctl->commands)) {
			GString* command = g_queue_pop_head(torctl->commands);

			gssize bytes = send(torctl->sd, command->str, command->len, 0);

			if(bytes > 0) {
				/* at least some parts of the command were sent successfully */
				GString* sent = g_string_new(command->str);
				sent = g_string_truncate(sent, bytes);
				torctl->slogf(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "torctl-sent '%s'", g_strchomp(sent->str));
				g_string_free(sent, TRUE);
			}

			if(bytes == command->len) {
				g_string_free(command, TRUE);
			} else {
				/* partial or no send */
				command = g_string_erase(command, (gssize)0, (gssize)bytes);
				g_queue_push_head(torctl->commands, command);
				break;
			}
		}
		guint32 event = g_queue_is_empty(torctl->commands) ? EPOLLIN : EPOLLOUT;
		_torctl_epoll(torctl, EPOLL_CTL_MOD, event);
	}

	/* recv and process all incoming lines */
	if(events & EPOLLIN) {
		torctl->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "EPOLLIN is set");

		gchar recvbuf[102400];
		memset(recvbuf, 0, 102400);
		gssize bytes = 0;

		while((bytes = recv(torctl->sd, recvbuf, 100000, 0)) > 0) {
			recvbuf[bytes] = '\0';

			gboolean isLastLineIncomplete = FALSE;
			if(bytes < 2 || recvbuf[bytes-2] != '\r' || recvbuf[bytes-1] != '\n') {
				isLastLineIncomplete = TRUE;
			}

			gchar** lines = g_strsplit(recvbuf, "\r\n", 0);
			gchar* line = NULL;
			for(gint i = 0; (line = lines[i]) != NULL; i++) {
				if(!torctl->receiveLineBuffer) {
					torctl->receiveLineBuffer = g_string_new(line);
				} else {
					g_string_append_printf(torctl->receiveLineBuffer, "%s", line);
				}

				if(!g_ascii_strcasecmp(line, "") ||
						(isLastLineIncomplete && lines[i+1] == NULL)) {
					/* this is '', or the last line, and its not all here yet */
					continue;
				} else {
					/* we have a full line in our buffer */
					_torctl_processLine(torctl, torctl->receiveLineBuffer);
					g_string_free(torctl->receiveLineBuffer, TRUE);
					torctl->receiveLineBuffer = NULL;
				}
			}
			g_strfreev(lines);
		}
	}

	/* if we have commands to send, lets register for output event */
	if(!g_queue_is_empty(torctl->commands)) {
		_torctl_epoll(torctl, EPOLL_CTL_MOD, EPOLLOUT);
	}
}

gboolean _torctl_start(TorCTL* torctl) {
	g_assert(torctl && (torctl->magic == MAGIC));

	/* use epoll to asynchronously watch events for all of our sockets */
	torctl->ed = epoll_create(1);
	if(torctl->ed == -1) {
		torctl->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "Error in main epoll_create");
		close(torctl->ed);
		return FALSE;
	}

	/* create the client socket and get a socket descriptor */
	torctl->sd = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
	if(torctl->sd == -1) {
		torctl->slogf(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__,
				"unable to start control socket: error in socket");
		return FALSE;
	}

	/* get the server ip address */
	if(g_ascii_strncasecmp(torctl->hostname->str, "localhost", 9) == 0) {
		torctl->netip = htonl(INADDR_LOOPBACK);
	} else {
		struct addrinfo* info;
		int ret = getaddrinfo(torctl->hostname->str, NULL, NULL, &info);
		if(ret >= 0) {
			torctl->netip = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
		}
		freeaddrinfo(info);
	}

	/* our client socket address information for connecting to the server */
	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = torctl->netip;
	serverAddress.sin_port = torctl->netport;

	/* connect to server. since we are non-blocking, we expect this to return EINPROGRESS */
	gint res = connect(torctl->sd, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
	if (res == -1 && errno != EINPROGRESS) {
		torctl->slogf(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__,
				"unable to start control socket: error in connect");
		return FALSE;
	}

	/* specify the events to watch for on this socket.
	 * to start out, the client wants to know when it can send a message. */
	_torctl_epoll(torctl, EPOLL_CTL_ADD, EPOLLOUT);
	torctl->state = TCS_NONE;

	return TRUE;
}

void torctl_free(TorCTL* torctl) {
	g_assert(torctl && (torctl->magic == MAGIC));

	if(torctl->sd) {
		close(torctl->sd);
	}

	if(torctl->ed) {
		close(torctl->ed);
	}

	if(torctl->receiveLineBuffer) {
		g_string_free(torctl->receiveLineBuffer, TRUE);
	}

	if(torctl->hostname) {
		g_string_free(torctl->hostname, TRUE);
	}

	if(torctl->eventsCommand) {
		g_string_free(torctl->eventsCommand, TRUE);
	}

	while(!g_queue_is_empty(torctl->commands)) {
		g_string_free(g_queue_pop_head(torctl->commands), TRUE);
	}
	g_queue_free(torctl->commands);

	torctl->magic = 0;
	g_free(torctl);
}

TorCTL* torctl_new(gint argc, gchar* argv[], ShadowLogFunc slogf) {
	g_assert(slogf);

	if(argc != 4) {
		slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, USAGE);
		return NULL;
	}

	TorCTL* torctl = g_new0(TorCTL, 1);
	torctl->magic = MAGIC;

	torctl->hostname = g_string_new(argv[1]);
	torctl->netport = (in_port_t) htons((in_port_t)atoi(argv[2]));

	gchar** eventStrs = g_strsplit(argv[3], ",", 0);
	gchar* eventStr = g_strjoinv(" ", eventStrs);
	torctl->eventsCommand = g_string_new(NULL);
	g_string_printf(torctl->eventsCommand, "SETEVENTS %s\r\n", eventStr);
	g_free(eventStr);
	g_strfreev(eventStrs);

	torctl->slogf = slogf;
	torctl->commands = g_queue_new();

	if(!_torctl_start(torctl)) {
		torctl_free(torctl);
		return NULL;
	}

	return torctl;
}

void torctl_ready(TorCTL* torctl) {
	g_assert(torctl && (torctl->magic == MAGIC));

	/* collect the events that are ready */
	struct epoll_event epevs[100];
	gint nfds = epoll_wait(torctl->ed, epevs, 100, 0);
	if(nfds == -1) {
		torctl->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "error in epoll_wait");
	} else {
		for(gint i = 0; i < nfds; i++) {
			gint d = epevs[i].data.fd;
			g_assert(d == torctl->sd);
			uint32_t e = epevs[i].events;
			_torctl_activate(torctl, e);
		}
	}
}

gint torctl_getEpollDescriptor(TorCTL* torctl) {
	g_assert(torctl && (torctl->magic == MAGIC));
	return torctl->ed;
}

gboolean torctl_isDone(TorCTL* torctl) {
	g_assert(torctl && (torctl->magic == MAGIC));
	return torctl->isDone;
}
