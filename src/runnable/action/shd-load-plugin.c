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

RunnableVTable loadplugin_vtable = {
	(RunnableRunFunc) loadplugin_run,
	(RunnableFreeFunc) loadplugin_free,
	MAGIC_VALUE
};

LoadPluginAction* loadplugin_new(GString* name, GString* path) {
	g_assert(name && path);
	LoadPluginAction* action = g_new0(LoadPluginAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadplugin_vtable);

	action->id = g_quark_from_string((const gchar*) name->str);
	action->path = g_string_new(path->str);

	return action;
}

void loadplugin_run(LoadPluginAction* action) {
	MAGIC_ASSERT(action);

	/* we need a copy of the library for every thread because each of
	 * them needs a separate instance of all the plug-in state so it doesn't
	 * overlap. We'll do this lazily while booting up applications, since that
	 * event will be run by a worker. For now, we just track the original
	 * filename of the plug-in library, so the worker can copy it later.
	 */
	Worker* worker = worker_getPrivate();

	/* the hash table now owns the GString */
	GQuark* id = g_new0(GQuark, 1);
	*id = action->id;
	registry_put(worker->cached_engine->registry, PLUGINPATHS, id, action->path->str);
}

void loadplugin_free(LoadPluginAction* action) {
	MAGIC_ASSERT(action);

	MAGIC_CLEAR(action);
	g_free(action);
}
