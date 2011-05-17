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

#ifndef _hashtable_h
#define _hashtable_h

#include "hash.h"
#include "btree.h"

#define hashtable_hashfunction(a) (a)

typedef struct hashtable_bucket_t {
	int single_key;
	void * single_data;
	btree_tp btree;
} hashtable_bucket_t, *hashtable_bucket_tp;

typedef struct hashtable_t {
	unsigned int num_buckets;
	unsigned int population;
	float growth_factor;

	hashtable_bucket_tp buckets;
} hashtable_t, *hashtable_tp;

typedef void (*hashtable_walk_callback_tp)(void *, int);

typedef void (*hashtable_walk_param_callback_tp)(void *, int, void *);

void hashtable_walk(hashtable_tp ht, hashtable_walk_callback_tp cb);

void hashtable_walk_param(hashtable_tp ht, hashtable_walk_param_callback_tp cb, void* param);

hashtable_tp hashtable_create(unsigned int buckets, float growth_factor);

void hashtable_rehash(hashtable_tp ht, int newsize);

void hashtable_destroy(hashtable_tp ht);

void hashtable_set(hashtable_tp ht, unsigned int key, void * value);

void * hashtable_get(hashtable_tp ht, unsigned int key) ;

void * hashtable_remove(hashtable_tp ht, unsigned int key);


#endif
