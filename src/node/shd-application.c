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

Application* application_new(GQuark id, GString* arguments, GString* pluginPath, SimulationTime startTime) {
	Application* application = g_new0(Application, 1);
	MAGIC_INIT(application);

	application->id = id;
	application->arguments = g_string_new(arguments->str);
	application->pluginPath = g_string_new(pluginPath->str);
	application->startTime = startTime;

	return application;
}

void application_free(gpointer data) {
	Application* application = data;
	MAGIC_ASSERT(application);

	g_string_free(application->arguments, TRUE);
	g_string_free(application->pluginPath, TRUE);

	MAGIC_CLEAR(application);
	g_free(application);
}
