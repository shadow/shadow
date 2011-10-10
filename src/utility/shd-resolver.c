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

#include <netinet/in.h>
#include <string.h>

void resolver_entry_destroy(gpointer data) {
	g_assert(data);

	resolver_entry_tp rentry = data;
	g_string_free(rentry->hostname, TRUE);
	g_free(rentry);
}

resolver_tp resolver_create() {
	resolver_tp r = g_new0(resolver_t, 1);

	r->unique_id_counter = 0;
	/* the key is stored in the value and freed when the value is */
	r->addr_entry = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, resolver_entry_destroy);
	/* the key is stored in the value, but the value is freed as part of addr_entry */
	r->name_entry = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

	return r;
}

void resolver_destroy(resolver_tp r) {
	g_assert(r);

	/* this doesnt free any keys or values */
	g_hash_table_destroy(r->name_entry);
	r->name_entry = NULL;
	/* frees values (and  keys implicitly since all keys are stored in the values) */
	g_hash_table_destroy(r->addr_entry);
	r->addr_entry = NULL;
	g_free(r);
}

/* name MUST be null-terminated */
void resolver_add(resolver_tp r, gchar* name, in_addr_t addr, guint8 prepend_unique_id, guint32 KBps_down, guint32 KBps_up) {
	g_assert(r);

	if(strlen(name) < 1) {
		/* in this case we always add a unique id */
		name = "default.shadow";
		prepend_unique_id = 1;
	}

	resolver_entry_tp rentry = g_new0(resolver_entry_t, 1);
	rentry->hostname = g_string_new(NULL);

	if(prepend_unique_id) {
		g_string_printf(rentry->hostname, "%u.%s", r->unique_id_counter++, name);
	} else {
		g_string_printf(rentry->hostname, "%s", name);
	}

	rentry->addr = addr;
	rentry->KBps_down = KBps_down;
	rentry->KBps_up = KBps_up;

	g_hash_table_insert(r->name_entry, rentry->hostname->str, rentry);
	g_hash_table_insert(r->addr_entry, &(rentry->addr), rentry);
}

static void resolver_remove_entry(resolver_tp r, resolver_entry_tp rentry) {
	g_assert(r);
	g_assert(rentry);
	g_assert(rentry->hostname);

	/* need to remove from name_entry first, since removing from
	 * addr_entry will cause the value to be freed
	 */
	g_hash_table_remove(r->name_entry, rentry->hostname->str);
	g_hash_table_remove(r->addr_entry, &(rentry->addr));
	g_free(rentry);
}

void resolver_remove_byname(resolver_tp r, gchar* name) {
	g_assert(r);

	resolver_entry_tp rentry = g_hash_table_lookup(r->name_entry, name);

	if(rentry != NULL) {
		resolver_remove_entry(r, rentry);
	}
}

void resolver_remove_byaddr(resolver_tp r, in_addr_t addr) {
	g_assert(r);

	resolver_entry_tp rentry = g_hash_table_lookup(r->addr_entry, &addr);

	if(rentry != NULL) {
		resolver_remove_entry(r, rentry);
	}
}

in_addr_t* resolver_resolve_byname(resolver_tp r, gchar* name) {
	g_assert(r);

	resolver_entry_tp rentry = g_hash_table_lookup(r->name_entry, name);

	if(rentry != NULL) {
		return &(rentry->addr);
	}

	return NULL;
}

gchar* resolver_resolve_byaddr(resolver_tp r, in_addr_t addr) {
	g_assert(r);

	resolver_entry_tp rentry = g_hash_table_lookup(r->addr_entry, &addr);

	if(rentry != NULL) {
		return rentry->hostname->str;
	}

	return NULL;
}

guint32 resolver_get_minbw(resolver_tp r, in_addr_t addr) {
	g_assert(r);

	resolver_entry_tp rentry = g_hash_table_lookup(r->addr_entry, &addr);

	if(rentry != NULL) {
		return rentry->KBps_down < rentry->KBps_up ? rentry->KBps_down : rentry->KBps_up;
	}

	return 0;
}

guint32 resolver_get_upbw(resolver_tp r, in_addr_t addr) {
	g_assert(r);

	resolver_entry_tp rentry = g_hash_table_lookup(r->addr_entry, &addr);

	if(rentry != NULL) {
		return rentry->KBps_up;
	}

	return 0;
}

guint32 resolver_get_downbw(resolver_tp r, in_addr_t addr) {
	g_assert(r);

	resolver_entry_tp rentry = g_hash_table_lookup(r->addr_entry, &addr);

	if(rentry != NULL) {
		return rentry->KBps_down;
	}

	return 0;
}
