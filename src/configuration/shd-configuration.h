/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#ifndef SHD_CONFIGURATION_H_
#define SHD_CONFIGURATION_H_

#include <glib.h>

/* time in nanoseconds */
typedef guint64 SimulationTime;
#define SIMTIME_INVALID G_MAXUINT64
#define SIMTIME_ONE_NANOSECOND G_GUINT64_CONSTANT(1)
#define SIMTIME_ONE_MICROSECOND G_GUINT64_CONSTANT(1000)
#define SIMTIME_ONE_MILLISECOND G_GUINT64_CONSTANT(1000000)
#define SIMTIME_ONE_SECOND G_GUINT64_CONSTANT(1000000000)
#define SIMTIME_ONE_MINUTE G_GUINT64_CONSTANT(60000000000)
#define SIMTIME_ONE_HOUR G_GUINT64_CONSTANT(3600000000000)

/* memory magic for assertions that memory has not been freed */
/* TODO add ifdef here so this stuff only happens in DEBUG mode */
#if 1
#define MAGIC_VALUE 0xAABBCCDD
#define MAGIC_DECLARE guint magic
#define MAGIC_INIT(object) object->magic = MAGIC_VALUE
#define MAGIC_ASSERT(object) g_assert(object && (object->magic == MAGIC_VALUE))
#define MAGIC_CLEAR(object) object->magic = 0
#else
#define MAGIC_VALUE
#define MAGIC_DECLARE
#define MAGIC_INIT(object)
#define MAGIC_ASSERT(object)
#define MAGIC_CLEAR(object)
#endif

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
 * FIXME we should implement socket descriptors greater than ugint16 so we can use
 * all 31 bits of the gint!
 */
#define VNETWORK_MIN_SD 30000

/* plug-ins must implement this method to hook into shadow */
#define PLUGININITSYMBOL "__shadow_plugin_init__"

#define NTOA(ip) inet_ntoa((struct in_addr){ip})

#define CONFIG_SEND_BUFFER_SIZE_FORCE 0
#define CONFIG_SEND_BUFFER_SIZE 131072
#define CONFIG_RECV_BUFFER_SIZE 174760
#define CONFIG_DO_DELAYED_ACKS 0

typedef struct _Configuration Configuration;

struct _Configuration {
	GOptionContext *context;

	gint nWorkerThreads;
	gint minTimeJump;
	gboolean printSoftwareVersion;

	GQueue* inputXMLFilenames;

	MAGIC_DECLARE;
};

Configuration* configuration_new(gint argc, gchar* argv[]);
void configuration_free(Configuration* config);

#endif /* SHD_CONFIGURATION_H_ */
