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

#include "shadow.h"

Application* application_new(Software* software) {
	Application* application = g_new0(Application, 1);
	MAGIC_INIT(application);
	g_assert(software);

	/* need to get thread-private plugin from current worker */
	Plugin* plugin = worker_getPlugin(software);

	application->software = software;
	application->state = plugin_newDefaultState(plugin);

	return application;
}

void application_free(Application* application) {
	MAGIC_ASSERT(application);

	/* need to get thread-private plugin from current worker */
	Plugin* plugin = worker_getPlugin(application->software);

	/* tell the plug-in module (user code) to free its data */
	plugin_executeFree(plugin, application->state);

	/* free our copy of plug-in resources */
	pluginstate_free(application->state);

	MAGIC_CLEAR(application);
	g_free(application);
}

void application_boot(Application* application) {
	MAGIC_ASSERT(application);

	/* get arguments from the configured software */
	gchar** argv;
	gint argc = software_getArguments(application->software, &argv);

	/* we will need to free each argument, copy argc in case they change it */
	gint n = argc;

	/* need to get thread-private plugin from current worker */
	Plugin* plugin = worker_getPlugin(application->software);
	plugin_executeNew(plugin, application->state, argc, argv);

	/* free the arguments */
	for(gint i = 0; i < n; i++) {
		g_free(argv[i]);
	}
	g_free(argv);
}

void application_notify(Application* application) {
	MAGIC_ASSERT(application);

	/* need to get thread-private plugin from current worker */
	Plugin* plugin = worker_getPlugin(application->software);
	plugin_executeNotify(plugin, application->state);
}
