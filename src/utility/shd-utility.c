/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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

guint utility_ipPortHash(in_addr_t ip, in_port_t port) {
	GString* buffer = g_string_new(NULL);
	g_string_printf(buffer, "%u:%u", ip, port);
	guint hash_value = g_str_hash(buffer->str);
	g_string_free(buffer, TRUE);
	return hash_value;
}

guint utility_int16Hash(gconstpointer value) {
	g_assert(value);
	/* make sure upper bits are zero */
	gint key = 0;
	key = (gint) *((gint16*)value);
	return g_int_hash(&key);
}

gboolean utility_int16Equal(gconstpointer value1, gconstpointer value2) {
	g_assert(value1 && value2);
	/* make sure upper bits are zero */
	gint key1 = 0, key2 = 0;
	key1 = (gint) *((gint16*)value1);
	key2 = (gint) *((gint16*)value2);
	return g_int_equal(&key1, &key2);
}
