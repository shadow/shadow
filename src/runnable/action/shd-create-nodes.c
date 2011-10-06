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

RunnableVTable createnodes_vtable = {
	(RunnableRunFunc) createnodes_run,
	(RunnableFreeFunc) createnodes_free,
	MAGIC_VALUE
};

CreateNodesAction* createnodes_new(guint seconds) {
	CreateNodesAction* action = g_new(CreateNodesAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnodes_vtable);

	return action;
}

void createnodes_run(CreateNodesAction* action) {
	MAGIC_ASSERT(action);
}

void createnodes_free(CreateNodesAction* action) {
	MAGIC_ASSERT(action);
	MAGIC_CLEAR(action);
	g_free(action);
}
