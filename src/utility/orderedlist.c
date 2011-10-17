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

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>

#include "orderedlist.h"

#define ORDEREDLIST_DEBUG 0

#if ORDEREDLIST_DEBUG
/* prints an ascii representation of the list */
static void orderedlist_print(orderedlist_tp list);
#endif
/* searches the list for the given key.
 * returns NULL if not found and the last element with the given key otherwise.
 */
static orderedlist_element_tp orderedlist_search(orderedlist_tp list, guint64 key);
/* searches for the position where key fits in the list.
 * returns NULL if the key belongs at the front of the list and the last element
 * that is <= the given key otherwise.
 */
static orderedlist_element_tp orderedlist_find_position(orderedlist_tp list, guint64 key);

orderedlist_tp orderedlist_create() {
	orderedlist_tp list = g_malloc(sizeof(orderedlist_t));

	list->first = NULL;
	list->last = NULL;
	list->length = 0;

	return list;
}

void orderedlist_destroy(orderedlist_tp list, guint8 do_free_values) {
	/* this will leak memory if the caller does not free the values stored in the
	 * list. we cannot free values since we do not know the type.
	 */
	if(list == NULL) {
		return;
	}

	if(do_free_values) {
		while (list->first != NULL) {
			gpointer value = orderedlist_remove_first(list);
			if(value != NULL){
				free(value);
			}
		}
	} else {
		while (list->first != NULL) {
			orderedlist_remove_first(list);
		}
	}

	free(list);
}

void orderedlist_add(orderedlist_tp list, guint64 key, gpointer value) {
	if(list == NULL || key == UINT32_MAX) {
		return;
	}

	/* create new element */
	orderedlist_element_tp new = g_malloc(sizeof(orderedlist_element_t));
	new->key = key;
	new->value = value;

	/* update new element links and list links */
	if(list->first == NULL) {
		/* list is empty */
		new->next = NULL;
		new->prev = NULL;
		list->first = new;
		list->last = new;
	} else {
		/* list is non-empty, find where to insert */
		orderedlist_element_tp target = orderedlist_find_position(list, key);

		/* if target not found, this is the minimum, ie first in list */
		if(target == NULL){
			new->next = list->first;
			new->prev = NULL;
			list->first->prev = new;
			list->first = new;
		} else if (target == list->last) {
			new->next = NULL;
			new->prev = list->last;
			list->last->next = new;
			list->last = new;
		} else {
			/* target is in the middle somewhere */
			new->next = target->next;
			new->prev = target;
			target->next->prev = new;
			target->next = new;
		}
	}

	list->length++;
}

gpointer orderedlist_remove(orderedlist_tp list, guint64 key) {
	if(list == NULL) {
		return NULL;
	}

	if(list->first == NULL) {
		/* list is empty */
		return NULL;
	}

	/* find what to remove */
	orderedlist_element_tp target = orderedlist_search(list, key);

	/* if target not found, we have nothing to remove */
	if(target == NULL){
		return NULL;
	}else if (target == list->last) {
		return orderedlist_remove_last(list);
	} else if (target == list->first) {
		return orderedlist_remove_first(list);
	}

	/* target is in middle somewhere */
	target->prev->next = target->next;
	target->next->prev = target->prev;
	gpointer value = target->value;
	free(target);
	list->length--;

	return value;
}

gpointer orderedlist_remove_first(orderedlist_tp list) {
	if(list == NULL || list->first == NULL){
		return NULL;
	}

	orderedlist_element_tp remove = list->first;
	if(list->first == list->last){
		/* list will be empty after remove */
		list->first = NULL;
		list->last = NULL;
	} else {
		list->first = list->first->next;
		list->first->prev = NULL;
	}

	/* save value for return */
	gpointer value = remove->value;
	free(remove);
	list->length--;

	return value;
}

gpointer orderedlist_remove_last(orderedlist_tp list) {
	if(list == NULL || list->last == NULL){
		return NULL;
	}

	orderedlist_element_tp remove = list->last;
	if(list->last == list->first){
		/* list will be empty after remove */
		list->last = NULL;
		list->first = NULL;
	} else {
		list->last = list->last->prev;
		list->last->next = NULL;
	}

	/* save value for return */
	gpointer value = remove->value;
	free(remove);
	list->length--;

	return value;
}

gpointer orderedlist_peek_first_value(orderedlist_tp list) {
	if(list == NULL || list->first == NULL){
		return NULL;
	}
	return list->first->value;
}

guint64 orderedlist_peek_first_key(orderedlist_tp list) {
	if(list == NULL || list->first == NULL){
		return UINT64_MAX;
	}
	return list->first->key;
}

gpointer orderedlist_peek_last_value(orderedlist_tp list) {
	if(list == NULL || list->last == NULL){
		return NULL;
	}
	return list->last->value;
}

guint64 orderedlist_peek_last_key(orderedlist_tp list) {
	if(list == NULL || list->last == NULL){
		return UINT64_MAX;
	}
	return list->last->key;
}

gpointer orderedlist_ceiling_value(orderedlist_tp list, guint64 key) {
	if(list == NULL) {
		return NULL;
	}

	orderedlist_element_tp floor = orderedlist_find_position(list, key);
	if(floor != NULL) {
		if(floor->key == key) {
			return floor->value;
		}else if(floor->next != NULL) {
			return floor->next->value;
		} else {
			return floor->value;
		}
	} else {
		return orderedlist_peek_first_value(list);
	}

	return NULL;
}

guint64 orderedlist_compact(orderedlist_tp list){
	if(list == NULL || list->first == NULL) {
		return 0;
	}

	guint32 count = 0;
	orderedlist_element_tp element = list->first;
	while(element != NULL) {
		element->key = count++;
		element = element->next;
	}

	/* count is the next available key in sequence */
	return count;
}

guint32 orderedlist_length(orderedlist_tp list){
	if(list == NULL) {
		return 0;
	} else {
		return list->length;
	}
}

static orderedlist_element_tp orderedlist_search(orderedlist_tp list, guint64 key) {
	orderedlist_element_tp target = list->last;
	while (target != NULL) {
		if(key == target->key) {
			break;
		}
		target = target->prev;
	}
	return target;
}

static orderedlist_element_tp orderedlist_find_position(orderedlist_tp list, guint64 key){
	/* we search from back of list, since increasing sequence numbers will
	 * get added to the back. this means adding to the list can be constant.
	 */
	orderedlist_element_tp target = list->last;
	while (target != NULL) {
		if(key >= target->key) {
			break;
		}
		target = target->prev;
	}
	return target;
}

#if ORDEREDLIST_DEBUG
static void orderedlist_print(orderedlist_tp list){
	printf("##########\n");
	if(list == NULL){
		printf("List:NULL\n");
		return;
	} else {
		printf("List [length:%u] ", list->length);
	}

	printf("[first:");
	if(list->first == NULL){
		printf("NULL] ");
	} else {
		printf("%u] ", list->first->key);
	}

	printf("[last:");
	if(list->last == NULL){
		printf("NULL]\n");
	} else {
		printf("%u]\n", list->last->key);
	}

	/* Traversing list from first to last */
	orderedlist_element_tp element = list->first;
	while(element != NULL){
		printf("element:%u\t", element->key);
		if(element->prev == NULL){
			printf("[prev:NULL]\t");
		} else {
			printf("[prev:%u]\t", element->prev->key);
		}
		if(element->next == NULL){
			printf("[next:NULL]\t");
		} else {
			printf("[next:%u]\t", element->next->key);
		}
		if(element->value == NULL){
			printf("[value:NULL]\n");
		} else {
			printf("[value:*]\n");
		}
		element = element->next;
	}

	/* done with traversal */
	printf("----------\n");
}
#endif
