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

	action->name = g_string_new(name->str);
	action->pluginName = g_string_new(pluginName->str);
	action->arguments = g_string_new(arguments->str);
	action->launchtime = (SimulationTime)launchtime;

	return action;
}

void createapplication_run(CreateApplicationAction* action) {
	MAGIC_ASSERT(action);

	Worker* worker = worker_getPrivate();

}

void createapplication_free(CreateApplicationAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->name, TRUE);
	g_string_free(action->pluginName, TRUE);
	g_string_free(action->arguments, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
