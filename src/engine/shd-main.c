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

#include "shadow.h"

Engine* shadow_engine;

gint shadow_main(gint argc, gchar* argv[]) {
	g_thread_init(NULL);

	/* setup configuration - this fails and aborts if invalid */
	Configuration* config = configuration_new(argc, argv);
	if(!config) {
		/* incorrect options given */
		return -1;
	}

	/* allocate application structures */
	shadow_engine = engine_new(config);

	g_log_set_default_handler(logging_handle_log, shadow_engine);
	debug("log system initialized");

	/* run the engine. when this returns, the simulation is done */
	debug("starting %i-threaded engine", shadow_engine->config->num_threads);
	gint retval = engine_run(shadow_engine);
	debug("engine finished, waiting for workers...");

	/* cleanup. workers are auto-deleted when threads end. */
	engine_free(shadow_engine);
	configuration_free(config);

	/* engine gone, must use glib logging */
	g_debug("exiting cleanly, returning value %i", retval);
	return retval;
}
