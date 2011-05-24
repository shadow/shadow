/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
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

#include <string.h>
#include <stdlib.h>

#include "global.h"
#include "hashtable.h"
#include "hash.h"

struct hashtable_cb_data {
    hashtable_walk_callback_tp cb;
    hashtable_walk_param_callback_tp cb_param;
    void *param;
};

hashtable_tp hashtable_create(unsigned int buckets, float growth_factor) {
	hashtable_tp ht = malloc(sizeof(*ht));

	if(!ht)
		printfault(EXIT_NOMEM,"Out of memory: hashtable_create");

	ht->ht = g_hash_table_new(g_int_hash, g_int_equal);

	return ht;
}

void hashtable_destroy(hashtable_tp ht) {
        g_hash_table_destroy(ht->ht);

	free(ht);
}

void hashtable_set(hashtable_tp ht, unsigned int key, void * value) {
	if(ht == NULL || ht->ht == NULL) 
		return;

        gint *k = g_new(gint, 1);
        *k = key;
        g_hash_table_insert(ht->ht, k, value);
}

void hashtable_foreach(gint key, gpointer value, gpointer data) {
    struct hashtable_cb_data *cb_data = data;

    if(cb_data->param == NULL) {
        (*cb_data->cb)(value, key);
    } else {
        (*cb_data->cb_param)(value, key, cb_data->param);
    }
}

void hashtable_walk(hashtable_tp ht, hashtable_walk_callback_tp cb) {
        struct hashtable_cb_data data;
        data.cb = cb;
        data.param = NULL;
        g_hash_table_foreach(ht->ht, (GHFunc)hashtable_foreach, &data);
}

void hashtable_walk_param(hashtable_tp ht, hashtable_walk_param_callback_tp cb, void* param) {
        struct hashtable_cb_data data;
        data.cb_param = cb;
        data.param = param;
        g_hash_table_foreach(ht->ht, (GHFunc)hashtable_foreach, &data);
}

void * hashtable_get(hashtable_tp ht, unsigned int key) {
	if(ht == NULL) 
		return NULL;

        return g_hash_table_lookup(ht->ht, &key);
}

void * hashtable_remove(hashtable_tp ht, unsigned int key) {
	if(ht == NULL) 
		return NULL;

        void *ret = g_hash_table_lookup(ht->ht, &key);
        g_hash_table_remove(ht->ht, &key);
	return ret;
}
