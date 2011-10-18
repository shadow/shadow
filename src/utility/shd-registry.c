/*
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

Registry* registry_new() {
	Registry* registry = g_new0(Registry, 1);
	MAGIC_INIT(registry);

	registry->storage = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, NULL);

	return registry;
}

static gboolean _registry_removeEntry(gpointer key, gpointer value, gpointer user_data) {
	GHashTable* entry = value;
	g_assert(entry);
	g_hash_table_destroy(entry);
	return TRUE;
}

void registry_free(Registry* registry) {
	MAGIC_ASSERT(registry);

	/*
	 * destroy each hashtable entry in storage. for each entry, this will
	 * trigger the user-supplied value_destory_func for all values.
	 */
	g_hash_table_foreach_remove(registry->storage, _registry_removeEntry, NULL);
	/* destroy storage, freeing the memory for our index keys */
	g_hash_table_destroy(registry->storage);

	MAGIC_CLEAR(registry);
	g_free(registry);
}

void registry_register(Registry* registry, gint index,
		GDestroyNotify keyDestroyFunc, GDestroyNotify valueDestroyFunc) {
	MAGIC_ASSERT(registry);

	/*
	 * create a new entry, i.e. track a new set of objects. we manage keys for
	 * the outer registry, but caller manages keys for this entry
	 */
	GHashTable* entry = g_hash_table_new_full(g_int_hash, g_int_equal, keyDestroyFunc, valueDestroyFunc);

	/* these will be freed when the storage table is destroyed */
	gint* storage_index = g_new0(gint, 1);
	*storage_index = index;

	g_hash_table_insert(registry->storage, storage_index, entry);
}

static GHashTable* _registry_getEntry(Registry* registry, gint index) {
	GHashTable* entry = g_hash_table_lookup(registry->storage, (gconstpointer)(&index));
	g_assert(entry);
	return entry;
}

void registry_put(Registry* registry, gint index, GQuark* key, gpointer value) {
	MAGIC_ASSERT(registry);

	/* simple insert into the hashtable stored at index */
	GHashTable* entry = _registry_getEntry(registry, index);

	/* make sure an object doesnt exist at this key */
	g_assert(!g_hash_table_lookup_extended(entry, (gconstpointer)key, NULL, NULL));

	g_hash_table_insert(entry, (gpointer)key, value);
}

gpointer registry_get(Registry* registry, gint index, GQuark* key) {
	MAGIC_ASSERT(registry);

	/* simple lookup from the hashtable stored at index */
	GHashTable* entry = _registry_getEntry(registry, index);
	return g_hash_table_lookup(entry, (gconstpointer)key);
}

GList* registry_getAll(Registry* registry, gint index) {
	MAGIC_ASSERT(registry);
	GHashTable* entry = _registry_getEntry(registry, index);
	return g_hash_table_get_values(entry);
}
