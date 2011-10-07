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

void action_init(Action* a, RunnableVTable* vtable) {
	g_assert(a && vtable);
	MAGIC_INIT(a);
	MAGIC_INIT(vtable);
	a->priority = 0;
	runnable_init(&(a->super), vtable);
}

gint action_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Action* aa = a;
	const Action* ab = b;
	MAGIC_ASSERT(aa);
	MAGIC_ASSERT(ab);
	return aa->priority > ab->priority ? +1 : aa->priority == ab->priority ? 0 : -1;
}
