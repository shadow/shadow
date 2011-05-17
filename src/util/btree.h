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

#ifndef _btree_h
#define _btree_h

/* fast, packed btree implementation that relies on userspace memory tracking to avoid
 * excessive malloc()s and free()s */

typedef struct btree_e_t {
	int parent, left, right;
	int v;
	void * d;
} btree_e_t, *btree_e_tp;

typedef struct btree_t {
	btree_e_tp elements;
	int num_elems;
	int allocated;
	int initial_size;
	int head_node;
} btree_t, * btree_tp;

typedef void (*btree_walk_callback_tp)(void *, int);

typedef void (*btree_walk_param_callback_tp)(void *, int, void *);

/**
 * Applies a user-supplied callback to every element inside the btree
 */
void btree_walk(btree_tp bt, btree_walk_callback_tp cb);

/**
 * Same, with optional user-specified parameter.
 */
void btree_walk_param(btree_tp bt, btree_walk_param_callback_tp cb, void* param);

/**
 * Creates a binary tree with the given initial size
 */
btree_tp btree_create( unsigned int initial_size) ;

/**
 * Returns the data at a given INDEX, not key.
 *
 * Index should be between 0 and the btree_get_size(bt)
 *
 * v is an optional output argument to get the key.
 *
 */
void * btree_get_index(btree_tp, unsigned int i, int * v);

/**
 * returns the number of elements in the btree
 */
#define btree_get_size(bt) ((bt)->num_elems)


/**
 * Destroys the given binary tree
 */
void btree_destroy(btree_tp bt) ;

/**
 * Returns the element in the btree, if it exists, for the given value
 */
void * btree_get(btree_tp bt, int v) ;

/**
 * Returns the head element in the btree, along with its key
 */
void * btree_get_head(btree_tp, int * v);

/**
 * Removes the element from the btree, if it exists, and returns it.
 */
void * btree_remove(btree_tp bt, int v) ;

/**
 * Inserts the given data into the btree using v as the value
 */
void btree_insert(btree_tp bt, int v, void *d) ;

#endif

