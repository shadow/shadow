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

#ifndef _list_h
#define _list_h

typedef int (*list_elem_isequal_fp)(void*, void*);

typedef struct list_elem_t {
	void * data;
	struct list_elem_t * next, *  prev;
} list_elem_t, *list_elem_tp;

typedef struct list_t {
	unsigned int num_elems;
	list_elem_tp first;
	list_elem_tp last;
} list_t, *list_tp;

typedef struct list_iter_s {
	list_elem_tp next;
}list_iter_t, *list_iter_tp;

/**
 * Creates a list object
 * @return newly created empty list
 */
list_tp list_create(void);

/**
 * Removes an element from the front of the list
 * @return The element removed
 */
void * list_pop_front(list_tp);

/**
 * Removes an element from the back of the list
 * @return The element removed
 */
void * list_pop_back(list_tp);

/**
 * @return The element at the front of the list
 */
void * list_get_front(list_tp);

/**
 * @return The element at the back of the list
 */
void * list_get_back(list_tp);

/**
 *
 */
void list_push_front(list_tp, void*);

/**
 *
 */
void list_push_back(list_tp, void*);

/**
 * @return The number of elements in the list
 */
unsigned int list_get_size(list_tp);

void* list_remove(list_tp, void*, list_elem_isequal_fp);

void* list_search(list_tp, void*, list_elem_isequal_fp);

/**
 * Destroys the list
 */
void list_destroy(list_tp);

list_iter_tp list_iterator_create(list_tp);
void list_iterator_destroy(list_iter_tp);
int list_iterator_hasnext(list_iter_tp);
void* list_iterator_getnext(list_iter_tp);

#endif
