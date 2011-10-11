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

RunnableVTable createapplication_vtable = {
	(RunnableRunFunc) createapplication_run,
	(RunnableFreeFunc) createapplication_free,
	MAGIC_VALUE
};

CreateApplicationAction* createapplication_new(GString* name,
		GString* pluginName, GString* arguments, guint64 launchtime)
{
	g_assert(name && pluginName && arguments);
	CreateApplicationAction* action = g_new0(CreateApplicationAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createapplication_vtable);

	action->id = g_quark_from_string((const gchar*) name->str);
	action->pluginID = g_quark_from_string((const gchar*) pluginName->str);
	action->arguments = g_string_new(arguments->str);
	action->launchtime = (SimulationTime) (launchtime * SIMTIME_ONE_SECOND);

	return action;
}

void createapplication_run(CreateApplicationAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();
	gchar* pluginPath = registry_get(worker->cached_engine->registry, PLUGINPATHS, &(action->pluginID));

	Application* application = application_new(action->id, action->arguments->str, pluginPath, action->launchtime);
	registry_put(worker->cached_engine->registry, APPLICATIONS, &(application->id), application);
}

void createapplication_free(CreateApplicationAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->arguments, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
