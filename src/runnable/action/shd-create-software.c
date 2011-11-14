/*
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

RunnableFunctionTable createsoftware_functions = {
	(RunnableRunFunc) createsoftware_run,
	(RunnableFreeFunc) createsoftware_free,
	MAGIC_VALUE
};

CreateSoftwareAction* createsoftware_new(GString* name,
		GString* pluginName, GString* arguments, guint64 launchtime)
{
	g_assert(name && pluginName && arguments);
	CreateSoftwareAction* action = g_new0(CreateSoftwareAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createsoftware_functions);

	action->id = g_quark_from_string((const gchar*) name->str);
	action->pluginID = g_quark_from_string((const gchar*) pluginName->str);
	action->arguments = g_string_new(arguments->str);
	action->launchtime = (SimulationTime) (launchtime * SIMTIME_ONE_SECOND);

	return action;
}

void createsoftware_run(CreateSoftwareAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();
	gchar* pluginPath = engine_get(worker->cached_engine, PLUGINPATHS, action->pluginID);

	Software* software = software_new(action->id, action->arguments->str, action->pluginID, pluginPath, action->launchtime);
	engine_put(worker->cached_engine, SOFTWARE, &(software->id), software);
}

void createsoftware_free(CreateSoftwareAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->arguments, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
