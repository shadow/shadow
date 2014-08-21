/*
 * See LICENSE for licensing information
 */

#include <string.h>
#include <arpa/inet.h>

#include "shd-tgen.h"

struct _TGen {
	ShadowLogFunc log;
	ShadowCreateCallbackFunc createCallback;

	TGenGraph* actionGraph;

	gint epollD;
	gint serverD;
	GHashTable* transfers;

	gboolean hasStarted;
	gboolean hasEnded;

	struct epoll_event* ee;

	guint magic;
};

/* store a global pointer to the log func, so we can log in any
 * of our tgen modules without a pointer to the tgen struct */
ShadowLogFunc tgenLogFunc;

static void _tgen_start(TGen* tgen) {
	TGEN_ASSERT(tgen);

	TGenAction* startAction = tgengraph_getStartAction(tgen->actionGraph);

	/* we run our protocol over a single server socket/port */
	tgen->serverD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (tgen->serverD <= 0) {
		tgen_critical("problem bootstrapping: socket() returned %i", tgen->serverD)
		tgen->serverD = 0;
		return;
	}

	/* setup the listener address information */
	struct sockaddr_in listener;
	memset(&listener, 0, sizeof(listener));
	listener.sin_family = AF_INET;
	listener.sin_addr.s_addr = INADDR_ANY;
	listener.sin_port = (in_port_t) tgenaction_getServerPort(startAction);

	/* bind the socket to the server port */
	gint result = bind(tgen->serverD, (struct sockaddr *) &listener, sizeof(listener));
	if (result < 0) {
		tgen_critical("problem bootstrapping: bind() returned %i", result)
		close(tgen->serverD);
		tgen->serverD = 0;
		return;
	}

	/* set as server listening socket */
	result = listen(tgen->serverD, SOMAXCONN);
	if (result < 0) {
		tgen_critical("problem bootstrapping: listen() returned %i", result)
		close(tgen->serverD);
		tgen->serverD = 0;
		return;
	}

	/* create an epoll descriptor so we can manage events */
	if(!tgen->epollD) {
		tgen->epollD = epoll_create(1);
		if(tgen->epollD < 0) {
			tgen_critical("problem bootstrapping: epoll_create() returned %i", tgen->epollD)
			close(tgen->epollD);
			tgen->epollD = 0;
			close(tgen->serverD);
			tgen->serverD = 0;
			return;
		}
	}

	/* start watching our server socket */
	tgen->ee->events = EPOLLIN|EPOLLOUT;
	tgen->ee->data.fd = tgen->serverD;
	result = epoll_ctl(tgen->epollD, EPOLL_CTL_ADD, tgen->serverD, tgen->ee);
	if(result != 0) {
		tgen_critical("problem bootstrapping: epoll_ctl() errno %i", errno);
		close(tgen->epollD);
		tgen->epollD = 0;
		close(tgen->serverD);
		tgen->serverD = 0;
		return;
	}

	/* if we got here, everything worked correctly! */
	tgen->hasStarted = TRUE;

	gchar ipStringBuffer[INET_ADDRSTRLEN+1];
	memset(ipStringBuffer, 0, INET_ADDRSTRLEN+1);
	inet_ntop(AF_INET, &listener.sin_addr.s_addr, ipStringBuffer, INET_ADDRSTRLEN);

	tgen_message("bootstrapping complete: server listening at %s:%u",
			ipStringBuffer, listener.sin_port);
}

TGen* tgen_new(gint argc, gchar* argv[], ShadowLogFunc logf, ShadowCreateCallbackFunc callf) {
	tgenLogFunc = logf;

	/* argv[0] is program name, argv[1] should be config file */
	if(argc != 2) {
		tgen_warning("USAGE: %s path/to/trafficgenerator.xml", argv[0]);
		return NULL;
	}

	/* parse the graphml config file */
	TGenGraph* graph = tgengraph_new(argv[1]);

	if(graph) {
		tgen_message("traffic generator config file '%s' passed validation", argv[1]);
	} else {
		tgen_warning("traffic generator config file '%s' failed validation", argv[1]);
		return NULL;
	}

	/* create the main driver object */
	TGen* tgen = g_new0(TGen, 1);
	tgen->magic = TGEN_MAGIC;

	tgen->log = logf;
	tgen->createCallback = callf;
	tgen->actionGraph = graph;
	tgen->transfers = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)tgentransfer_free);
	tgen->ee = calloc(1, sizeof(struct epoll_event));

	tgen_debug("set log function to %p, callback function to %p", logf, callf);

	_tgen_start(tgen);

	return tgen;
}

void tgen_free(TGen* tgen) {
	TGEN_ASSERT(tgen);
	if(tgen->ee) {
		g_free(tgen->ee);
	}
	if(tgen->transfers) {
		g_hash_table_destroy(tgen->transfers);
	}
	if(tgen->serverD) {
		close(tgen->serverD);
	}
	if(tgen->epollD) {
		close(tgen->epollD);
	}
	if(tgen->actionGraph) {
		tgengraph_free(tgen->actionGraph);
	}
	tgen->magic = 0;
	g_free(tgen);
}

void tgen_activate(TGen* tgen) {
	TGEN_ASSERT(tgen);

	if(!tgen->hasStarted) {
		return;
	}

	/* collect the events that are ready */
	struct epoll_event epevs[10];
	gint nfds = epoll_wait(tgen->epollD, epevs, 10, 0);
	if(nfds == -1) {
		tgen_critical("error in client epoll_wait");
	}

	/* activate correct component for every socket thats ready */
	for(gint i = 0; i < nfds; i++) {
		gint socketD = epevs[i].data.fd;
		if(socketD == tgen->serverD) {
			// TODO accept new connection
		} else {
			TGenTransfer* transfer = g_hash_table_lookup(tgen->transfers, GINT_TO_POINTER(socketD));
			if(!transfer) {
				tgen_warning("can't find transfer for socket descriptor '%i'", socketD);
				continue;
			}

			// TODO do something with transfer
		}
	}
}

gint tgen_getEpollDescriptor(TGen* tgen) {
	TGEN_ASSERT(tgen);
	return tgen->epollD;
}

gboolean tgen_hasStarted(TGen* tgen) {
	TGEN_ASSERT(tgen);
	return tgen->hasStarted;
}

gboolean tgen_hasEnded(TGen* tgen) {
	TGEN_ASSERT(tgen);
	return tgen->hasEnded;
}
