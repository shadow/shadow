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

/* plug-ins must implement this method to hook into shadow */
#define PLUGININITSYMBOL "__shadow_plugin_init__"

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
