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

#include <string.h>

Software* software_new(GQuark id, gchar* arguments, GQuark pluginID, gchar* pluginPath, SimulationTime startTime) {
	Software* software = g_new0(Software, 1);
	MAGIC_INIT(software);

	software->id = id;
	software->arguments = g_string_new(arguments);
	software->pluginID = pluginID;
	software->pluginPath = g_string_new(pluginPath);
	software->startTime = startTime;

	return software;
}

void software_free(gpointer data) {
	Software* software = data;
	MAGIC_ASSERT(software);

	g_string_free(software->arguments, TRUE);
	g_string_free(software->pluginPath, TRUE);

	MAGIC_CLEAR(software);
	g_free(software);
}

gint software_getArguments(Software* software, gchar** argvOut[]) {
	MAGIC_ASSERT(software);

	gchar* argumentString = g_strdup(software->arguments->str);
	GQueue *arguments = g_queue_new();

	/* parse the full argument string into separate strings */
	gchar* token = strtok(argumentString, " ");
	while(token != NULL) {
		gchar* argument = g_strdup((const gchar*) token);
		g_queue_push_tail(arguments, argument);
		token = strtok(NULL, " ");
	}

	/* setup for creating new plug-in, i.e. format into argc and argv */
	gint argc = g_queue_get_length(arguments);
	/* a pointer to an array that holds pointers */
	gchar** argv = g_new0(gchar*, argc);

	for(gint i = 0; i < argc; i++) {
		argv[i] = g_queue_pop_head(arguments);
	}

	/* cleanup */
	g_free(argumentString);
	g_queue_free(arguments);

	/* transfer to the caller - they must free argv and each element of it */
	*argvOut = argv;
	return argc;
}
