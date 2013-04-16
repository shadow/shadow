/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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

gchar* utility_getHomePath(const gchar* path) {
	GString* sbuffer = g_string_new("");
	if(g_ascii_strncasecmp(path, "~", 1) == 0) {
		/* replace ~ with home directory */
		const gchar* home = g_get_home_dir();
		g_string_append_printf(sbuffer, "%s%s", home, path+1);
	} else {
		g_string_append_printf(sbuffer, "%s", path);
	}
	return g_string_free(sbuffer, FALSE);
}
