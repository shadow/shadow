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
