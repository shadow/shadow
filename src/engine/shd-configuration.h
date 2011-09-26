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

typedef struct _Configuration Configuration;

struct _Configuration {
	GOptionContext *context;
	gint num_threads;
};

Configuration* configuration_new(gint argc, gchar* argv[]);
void configuration_free(Configuration* config);

#endif /* SHD_CONFIGURATION_H_ */
