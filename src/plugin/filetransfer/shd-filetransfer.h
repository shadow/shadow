/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */


#ifndef SHD_FILETRANSFER_H_
#define SHD_FILETRANSFER_H_

/*
 * This lib provides a minimal http/socks client, socks proxy, and http server.
 *
 * Example http request we support:
 * 	"GET /path/to/file HTTP/1.1\r\nHost: www.somehost.com\r\n\r\n"
 *
 * Example http reply we support:
 *  "HTTP/1.1 404 NOT FOUND\r\n"
 *  "HTTP/1.1 200 OK\r\nContent-Length: 17\r\n\r\nSome data payload"
 */

#include <glib.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include <shd-library.h>

#include "shd-filetransfer-defs.h"
#include "shd-fileserver.h"
#include "shd-filegetter.h"
#include "shd-service-filegetter.h"

typedef struct _FileTransfer FileTransfer;
struct _FileTransfer {
	ShadowFunctionTable* shadowlib;
	service_filegetter_tp client;
	fileserver_tp server;
};

void filetransfer_init(FileTransfer* existingFT);
void filetransfer_new(int argc, char* argv[]);
void filetransfer_free();
void filetransfer_activate();

#endif /* SHD_FILETRANSFER_H_ */
