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

#ifndef _global_h
#define _global_h

#include <stdarg.h>
#include <stdint.h>
#include "utility.h"

/* from autotools */
#include "config.h"


#ifndef NULL
#define NULL 0
#endif

/* EXIT ERROR CODES */
#define EXIT_OK 0
#define EXIT_NOMEM 1
#define EXIT_UNKNOWN 2

typedef unsigned long long ptime_t;

#define PTIME_INVALID 0
#define PTIME_MAX UINT64_MAX

/* We intercept all socket calls made in DVN so we can call our virtual socket
 * functions instead of the system socket functions. However, there are cases
 * where DVN-core actually wants to create a real system socket - for
 * communicating with other DVN slaves if running a distributed simulation.
 * We use the following constant to inform the preload library that the call
 * was made from DVN and should be forwarded to the regular system socket call.

 * If DVN wants a real socket, do this:
 * fd = socket(AF_INET, SOCK_STREAM | DVN_CORE_SOCKET, 0);
 *
 * CAUTION: we are using a _currently_ unused bit from bits/socket.h types to
 * differentiate between DVN socket calls and module socket calls. If the socket
 * library starts using this bit, we need to change our constant.
 */
#define DVN_CORE_SOCKET 0x20

/* We intercept read, write, and close calls since they may be done on our
 * virtual sockets. However, applications may also want to read/write/close a
 * regular file. We differentiate these by handing out high descriptors for
 * our virtual sockets. Any descriptor below this cutoff can be considered a
 * real file.
 *
 * It is important to set this high enough so in large simulations the system
 * file descriptor counter doesnt collide with our sockets. So this should be
 * set over the ulimit -n value.
 *
 * FIXME we should implement socket descriptors greater than uint16 so we can use
 * all 31 bits of the int!
 */
#define VNETWORK_MIN_SD 30000

/* todo probably doesnt belong here*/
enum operation_type {
	OP_NULL, OP_LOAD_PLUGIN, OP_LOAD_CDF, OP_GENERATE_CDF, OP_CREATE_NETWORK,
	OP_CONNECT_NETWORKS, OP_CREATE_HOSTNAME, OP_CREATE_NODES, OP_END,
};

#endif

