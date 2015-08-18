/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ECHO_H_
#define SHD_ECHO_H_

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

#define BUFFERSIZE 20000
#define ECHO_SERVER_PORT 9999
#define MAX_EVENTS 10

/**
 * Note:
 * If a module contains a function named g_module_check_init() it is called
 * automatically when the module is loaded. It is passed the GModule structure
 * and should return NULL on success or a string describing the initialization
 * error. Similarly, if a module contains a function g_module_unload() it is
 * called by GLib right before the module is unloaded.
 *
 * g_module_check_init(GModule* module)
 * g_module_unload(GModule* module)
 *
 * @param module :
 *	 the GModule corresponding to the module which has just been loaded.
 * Returns :
 *   NULL on success, or a string describing the initialization error.
 */

/**
 * Protocol modes this echo module supports.
 */
enum EchoProtocol {
	ECHOP_NONE, ECHOP_TCP, ECHOP_UDP, ECHOP_PIPE,
};

/**
 *
 */
typedef struct _EchoClient EchoClient;
struct _EchoClient {
	ShadowLogFunc log;
	in_addr_t serverIP;
	gint epolld;
	gint socketd;
	gchar sendBuffer[BUFFERSIZE];
	gchar recvBuffer[BUFFERSIZE];
	gint recv_offset;
	gint sent_msg;
	gint amount_sent;
	gint is_done;
};

/**
 *
 */
typedef struct _EchoServer EchoServer;
struct _EchoServer {
	ShadowLogFunc log;
	gint epolld;
	gint listend;
	gint socketd;
	struct sockaddr_in address;
	gchar echoBuffer[BUFFERSIZE];
	gint read_offset;
	gint write_offset;
};

/**
 *
 */
typedef struct _EchoTCP EchoTCP;
struct _EchoTCP {
	ShadowLogFunc log;
	EchoClient* client;
	EchoServer* server;
};

/**
 *
 */
typedef struct _EchoUDP EchoUDP;
struct _EchoUDP {
	ShadowLogFunc log;
	EchoClient* client;
	EchoServer* server;
};

/**
 *
 */
typedef struct _EchoPipe EchoPipe;
struct _EchoPipe {
	ShadowLogFunc log;
	gint writefd;
	gchar inputBuffer[BUFFERSIZE];
	gboolean didWrite;
	gint readfd;
	gchar outputBuffer[BUFFERSIZE];
	gint didRead;
	gint epolld;
};

/**
 *
 */
typedef struct _Echo Echo;
struct _Echo {
	ShadowFunctionTable shadowlibFuncs;
	enum EchoProtocol protocol;
	EchoTCP* etcp;
	EchoUDP* eudp;
	EchoPipe* epipe;
};

void echoplugin_new(int argc, char* argv[]);
void echoplugin_free();
void echoplugin_ready();

EchoTCP* echotcp_new(ShadowLogFunc log, int argc, char* argv[]);
void echotcp_free(EchoTCP* etcp);
void echotcp_ready(EchoTCP* etcp);

EchoUDP* echoudp_new(ShadowLogFunc log, int argc, char* argv[]);
void echoudp_free(EchoUDP* eudp);
void echoudp_ready(EchoUDP* eudp);

EchoPipe* echopipe_new(ShadowLogFunc log);
void echopipe_free(EchoPipe* epipe);
void echopipe_ready(EchoPipe* epipe);

#endif /* SHD_ECHO_H_ */
