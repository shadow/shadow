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

LoadPluginAction* loadplugin_new(gint id, Registry* registry, GString* filename) {
	g_assert(registry && filename);
	LoadPluginAction* action = g_new0(LoadPluginAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &loadplugin_vtable);

	action->id = id;
	action->registry = registry;
	action->filename = filename;

	return action;
}

void loadplugin_run(LoadPluginAction* action) {
	MAGIC_ASSERT(action);

//	module_tp mod = module_load(wo->mod_mgr, op->id, op->filepath);
//
//	if(mod != NULL) {
//		context_execute_init(mod);
//	} else {
//		gchar buffer[200];
//
//		snprintf(buffer,200,"Unable to load and validate '%s'", op->filepath);
//		sim_worker_abortsim(wo, buffer);
//	}
}

void loadplugin_free(LoadPluginAction* action) {
	MAGIC_ASSERT(action);
	MAGIC_CLEAR(action);
	g_free(action);
}
