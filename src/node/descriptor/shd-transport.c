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

DescriptorFunctionTable transport_functions = {
	(DescriptorFreeFunc) transport_free,
	MAGIC_VALUE
};

void transport_init(Transport* transport, TransportFunctionTable* vtable, enum DescriptorType type, gint handle) {
	g_assert(transport && vtable);

	descriptor_init(&(transport->super), type, &transport_functions, handle);

	MAGIC_INIT(transport);
	MAGIC_INIT(vtable);

	transport->vtable = vtable;
}

void transport_free(gpointer data) {
	Transport* transport = data;
	MAGIC_ASSERT(transport);
	MAGIC_ASSERT(transport->vtable);

	MAGIC_CLEAR(transport);
	transport->vtable->free(transport);
}
