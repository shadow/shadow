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

#include <stdlib.h>
#include "global.h"
#include "btree.h"

btree_tp btree_create( unsigned int initial_size) {
	btree_tp bt;

	bt = malloc(sizeof(*bt));
	if(!bt)
		printfault(EXIT_NOMEM, "Out of memory: btree_create");

	bt->allocated = initial_size;
	if(bt->allocated) {
		bt->elements = malloc(sizeof(*bt->elements) * bt->allocated);
		if(!bt->elements) {
			bt->allocated = 0;
			bt->elements = NULL;
		}
	} else
		bt->elements = NULL;
	bt->initial_size = initial_size;
	bt->num_elems = 0;
	bt->head_node = -1;

	return bt;
}

void * btree_get_head(btree_tp bt, int * v) {
	void * rv = NULL;
	if(bt && bt->num_elems > 0) {
		rv = bt->elements[0].d;
		if(v)
			*v = bt->elements[0].v;
	}
	return rv;
}

void btree_destroy(btree_tp bt) {
	if(bt) {
		if(bt->elements) {
			free(bt->elements);
			bt->elements = NULL;
		}
		free(bt);
	}
}

void * btree_get(btree_tp bt, int v) {
	int cur=bt->head_node;

	if(!bt || bt->num_elems == 0)
		return NULL;

	do {
		if(v == bt->elements[cur].v)
			return bt->elements[cur].d;
		else if(v < bt->elements[cur].v)
			cur = bt->elements[cur].left;
		else
			cur = bt->elements[cur].right;
	}while(cur != -1);

	return NULL;
}

void * btree_remove(btree_tp bt, int v) {
	int cur=bt->head_node, *next=NULL;
	void * rv;
	int replacement = -1;
	int new_allocated;

	if(!bt || bt->num_elems == 0)
		return NULL;

	if(bt->num_elems == 1) {
		if(bt->elements[0].v == v) {
			bt->num_elems--;
			return bt->elements[0].d;
		} else
			return NULL;
	}

	do {
		if(v == bt->elements[cur].v) {
			/* found it! */

			/* first, find the replacement element */
			if(bt->elements[cur].left == -1)
				replacement = bt->elements[cur].right;

			else if(bt->elements[cur].right == -1)
				replacement = bt->elements[cur].left;

			else {
				replacement = bt->elements[cur].left;

				if(bt->elements[replacement].right == -1)
					bt->elements[replacement].right = bt->elements[cur].right;
				else {
					while(bt->elements[replacement].right != -1)
						replacement = bt->elements[replacement].right;

					/* save the left tree of the replacement element... */
					bt->elements[ bt->elements[replacement].parent ].right = bt->elements[replacement].left;
					if(bt->elements[replacement].left != -1)
						bt->elements[ bt->elements[replacement].left ].parent = bt->elements[replacement].parent;

					/* update the replacement element so it fits properly */
					bt->elements[replacement].left = bt->elements[cur].left;
					bt->elements[replacement].right = bt->elements[cur].right;
				}
			}

			if(replacement != -1) {
				/* update the replacement node's parent */
				bt->elements[replacement].parent = bt->elements[cur].parent;

				/* update original node's children to new parent */
				if(bt->elements[cur].left != -1 && bt->elements[cur].left != replacement)
					bt->elements[ bt->elements[cur].left ].parent = replacement;
				if(bt->elements[cur].right != -1 && bt->elements[cur].right != replacement)
					bt->elements[ bt->elements[cur].right ].parent = replacement;
			}

			if(next) /* adjust parent to the new child */
				*next = replacement;
			else /* replacement is the new head node. */
				bt->head_node = replacement;

			/* save the return value */
			rv = bt->elements[cur].d;

			/* now memory adjustments */
			bt->num_elems--;
			if(cur < bt->num_elems) {
				int swapfrom = bt->num_elems;

				/* overwrite cur with the last element */
				bt->elements[cur].v = bt->elements[swapfrom].v;
				bt->elements[cur].left = bt->elements[swapfrom].left;
				bt->elements[cur].right = bt->elements[swapfrom].right;
				bt->elements[cur].d = bt->elements[swapfrom].d;
				bt->elements[cur].parent = bt->elements[swapfrom].parent;

				/* change the parent to point to new child */
				if(bt->elements[cur].parent != -1) {
					if(bt->elements[bt->elements[cur].parent].left == swapfrom)
						bt->elements[bt->elements[cur].parent].left = cur;
					else
						bt->elements[bt->elements[cur].parent].right = cur;
				} else
					bt->head_node = cur;

				/* change children of old element to point to new element */
				if(bt->elements[cur].left != -1)
					bt->elements[bt->elements[cur].left].parent = cur;
				if(bt->elements[cur].right != -1)
					bt->elements[bt->elements[cur].right].parent = cur;
			}

			/* check sizing... */
			new_allocated = bt->allocated/2;
			if(bt->num_elems <= new_allocated && new_allocated >= bt->initial_size) {
				if(new_allocated == 0) {
					free(bt->elements);
					bt->elements = NULL;
				} else
					bt->elements = realloc(bt->elements, sizeof(*bt->elements)*new_allocated);
				bt->allocated = new_allocated;
			}

			return rv;
		} else if(v < bt->elements[cur].v)
			next = &bt->elements[cur].left;
		else
			next = &bt->elements[cur].right;

		cur = *next;
	} while(cur != -1);

	return NULL;
}

void btree_walk(btree_tp bt, btree_walk_callback_tp cb) {
	int i;
	for(i=0; i<bt->num_elems; i++)
		(*cb)(bt->elements[i].d, bt->elements[i].v);
}

void btree_walk_param(btree_tp bt, btree_walk_param_callback_tp cb, void* param) {
	int i;
	for(i=0; i<bt->num_elems; i++)
		(*cb)(bt->elements[i].d, bt->elements[i].v, param);
}

void * btree_get_index(btree_tp bt, unsigned int i, int * v) {
	if(i>=bt->num_elems)
		return NULL;
	else {
		if(v)
			*v = bt->elements[i].v;
		return bt->elements[i].d;
	}
}

void btree_insert(btree_tp bt, int v, void *d) {
	int cur=bt->head_node, *next, parent=-1;

	if(d == NULL)
		btree_remove(bt, v);
	else {
		/* ensure sizing */
		if(bt->num_elems == bt->allocated) {
			if(bt->allocated == 0)
				bt->allocated = 8;
			else
				bt->allocated *= 2;
			if(bt->elements != NULL)
				bt->elements = realloc(bt->elements, sizeof(*bt->elements)*bt->allocated);
			else
				bt->elements = malloc(sizeof(*bt->elements)*bt->allocated);
			if(!bt->elements)
				printfault(EXIT_NOMEM, "Out of memory: btree_insert");
		}

		if(bt->num_elems == 0) {
			bt->elements[0].d = d;
			bt->elements[0].v = v;
			bt->elements[0].parent = bt->elements[0].left = bt->elements[0].right = -1;
			bt->head_node = 0;
		} else {
			do {
				if(v < bt->elements[cur].v)
					next = &bt->elements[cur].left;
				else
					next = &bt->elements[cur].right;

				if(*next == -1) {
					parent = cur;
					cur = bt->num_elems;
					*next = cur;
					break;
				} else
					cur = *next;

			} while(1);

			bt->elements[cur].d = d;
			bt->elements[cur].v = v;
			bt->elements[cur].parent = parent;
			bt->elements[cur].left = bt->elements[cur].right = -1;
		}
		bt->num_elems++;
	}

	return;
}
