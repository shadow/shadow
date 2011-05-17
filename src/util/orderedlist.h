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

#ifndef ORDEREDLIST_H_
#define ORDEREDLIST_H_

#include <stdint.h>

typedef struct orderedlist_element_t {
	uint64_t key;
	void* value;
	struct orderedlist_element_t* next;
	struct orderedlist_element_t* prev;
} orderedlist_element_t, *orderedlist_element_tp;

typedef struct orderedlist_t {
	uint32_t length;
	orderedlist_element_tp first;
	orderedlist_element_tp last;
} orderedlist_t, *orderedlist_tp;

/* creates a new empty list.
 * returns a reference to the new list.
 */
orderedlist_tp orderedlist_create();
/*
 * removes all elements and destroys the list. if do_free_values!=0, the memory
 * in each element's value field will also be destroyed.
 */
void orderedlist_destroy(orderedlist_tp list, uint8_t do_free_values);
/*
 * adds a new element storing the given value to the list. the element will be
 * added to the last position <= the given key, or the front if no such
 * position exists. the element WILL NOT BE ADDED if the given key is set to
 * UINT64_MAX, as that value is reserved for the list.
 */
void orderedlist_add(orderedlist_tp list, uint64_t key, void* value);
/*
 * removes the last element with the given key from the list.
 * returns the value stored at that element, or NULL if such an element does not
 * exist.
 */
void* orderedlist_remove(orderedlist_tp list, uint64_t key);
/*
 * removes the first element from the list.
 * returns the value stored at that element, or NULL if the list is empty.
 */
void* orderedlist_remove_first(orderedlist_tp list);
/*
 * removes the last element from the list.
 * returns the value stored at that element, or NULL if the list is empty.
 */
void* orderedlist_remove_last(orderedlist_tp list);
/*
 * returns the value stored at the list's first element, or NULL if the list
 * is empty
 */
void* orderedlist_peek_first_value(orderedlist_tp list);
/*
 * returns the key stored at the list's first element, or UINT64_MAX if the list
 * is empty
 */
uint64_t orderedlist_peek_first_key(orderedlist_tp list);
/*
 * returns the value stored at the list's last element, or NULL if the list
 * is empty
 */
void* orderedlist_peek_last_value(orderedlist_tp list);
/*
 * returns the key stored at the list's last element, or UINT64_MAX if the list
 * is empty
 */
uint64_t orderedlist_peek_last_key(orderedlist_tp list);
/* returns the value of the smallest key >= given key */
void* orderedlist_ceiling_value(orderedlist_tp list, uint64_t key);
/*
 * adjusts the keys of all elements in the list such the each element's new key
 * is it's position in the list.
 * returns the next unused key position, which is also the length of the list.
 */
uint64_t orderedlist_compact(orderedlist_tp list);

/*
 * returns the number of elements in the list, or 0 if the list is empty or NULL.
 */
uint32_t orderedlist_length(orderedlist_tp list);

#endif /* ORDEREDLIST_H_ */
