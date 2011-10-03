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

#ifndef SHD_REGISTRY_H_
#define SHD_REGISTRY_H_

#include "shadow.h"

typedef struct _Registry Registry;

struct _Registry {
	GHashTable* storage;

	MAGIC_DECLARE;
};

Registry* registry_new();
void registry_free(Registry* registry);

void registry_register(Registry* registry, gint index, GDestroyNotify value_destory_func);
void registry_put(Registry* registry, gint index, gint* key, gpointer value);
gpointer registry_get(Registry* registry, gint index, gint* key);

#endif /* SHD_REGISTRY_H_ */
