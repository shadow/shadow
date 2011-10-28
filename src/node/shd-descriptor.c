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

void descriptor_init(Descriptor* descriptor, enum DescriptorType type,
		DescriptorFunctionTable* funcTable, gint handle) {
	g_assert(descriptor && funcTable);
	g_assert(handle >= VNETWORK_MIN_SD);

	MAGIC_INIT(descriptor);
	MAGIC_INIT(funcTable);
	descriptor->funcTable = funcTable;
	descriptor->handle = handle;
	descriptor->type = type;
}

void descriptor_free(gpointer data) {
	Descriptor* descriptor = data;
	MAGIC_ASSERT(descriptor);
	MAGIC_ASSERT(descriptor->funcTable);

	MAGIC_CLEAR(descriptor);
	descriptor->funcTable->free(descriptor);
}

gint descriptor_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
	const Descriptor* foo = a;
	const Descriptor* bar = b;
	MAGIC_ASSERT(foo);
	MAGIC_ASSERT(bar);
	return foo->handle > bar->handle ? +1 : foo->handle == bar->handle ? 0 : -1;
}

enum DescriptorType descriptor_getType(Descriptor* descriptor) {
	MAGIC_ASSERT(descriptor);
	return descriptor->type;
}

gint* descriptor_getHandleReference(Descriptor* descriptor) {
	MAGIC_ASSERT(descriptor);
	return &(descriptor->handle);
}
