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

RunnableVTable createhostname_vtable = {
	(RunnableRunFunc) createhostname_run,
	(RunnableFreeFunc) createhostname_free,
	MAGIC_VALUE
};

CreateHostnameAction* createhostname_new(guint seconds) {
	CreateHostnameAction* action = g_new0(CreateHostnameAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createhostname_vtable);

	return action;
}

void createhostname_run(CreateHostnameAction* action) {
	MAGIC_ASSERT(action);
}

void createhostname_free(CreateHostnameAction* action) {
	MAGIC_ASSERT(action);
	MAGIC_CLEAR(action);
	g_free(action);
}
