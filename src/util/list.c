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

#include <unistd.h>
#include <stdlib.h>

#include "global.h"
#include "list.h"

static list_elem_tp list_search_internal(list_tp haystack, void* needle, list_elem_isequal_fp isequal);

list_tp list_create(){
	list_tp list = malloc(sizeof(*list));

	if(!list)
		printfault(EXIT_NOMEM, "list_create: Out of memory");

	list->first = list->last = NULL;
	list->num_elems = 0;

	return list;
}

void * list_pop_front(list_tp list) {
	list_elem_tp to_remove;
	void * rv;

	if(!list || !list->first)
		return NULL;

	to_remove = list->first;
	rv = to_remove->data;

	list->first = list->first->next;
	if(list->first)
		list->first->prev = NULL;
	else
		list->last = NULL;

	free(to_remove);
	list->num_elems--;

	return rv;
}

void * list_pop_back(list_tp list) {
	list_elem_tp to_remove;
	void * rv;

	if(!list || !list->last)
		return NULL;

	to_remove = list->last;
	rv = to_remove->data;

	list->last = list->last->prev;
	if(list->last)
		list->last->next = NULL;
	else
		list->first = NULL;

	free(to_remove);
	list->num_elems--;

	return rv;
}

void * list_get_front(list_tp list) {
	if(!list || !list->first)
		return NULL;
	return list->first->data;
}

void * list_get_back(list_tp list) {
	if(!list || !list->last)
		return NULL;
	return list->last->data;
}

void list_push_front(list_tp list, void *data) {
	list_elem_tp new_front = malloc(sizeof(*new_front));

	if(!new_front)
		printfault(EXIT_NOMEM, "list_push_front: Out of memory");

	new_front->data = data;
	new_front->next = list->first;
	new_front->prev = NULL;

	list->first = new_front;

	if(new_front->next)
		new_front->next->prev = new_front;

	if(list->last == NULL)
		list->last = new_front;

	list->num_elems++;

	return;
}

void list_push_back(list_tp list, void *data) {
	list_elem_tp new_back = malloc(sizeof(*new_back));

	if(!new_back)
		printfault(EXIT_NOMEM, "list_push_back: Out of memory");

	new_back->data = data;
	new_back->next = NULL;
	new_back->prev = list->last;

	list->last = new_back;

	if(new_back->prev)
		new_back->prev->next = new_back;

	if(list->first == NULL)
		list->first = new_back;

	list->num_elems++;

	return;
}

unsigned int list_get_size(list_tp list) {
	return list->num_elems;
}

/* search for needle in haystack, using isequal as comparator.
 * if isequal is NULL, uses simple == operator.
 * uses list_search: if found, remove and return the item, else return NULL. */
void* list_remove(list_tp haystack, void* needle, list_elem_isequal_fp isequal) {
	void* search_needle = NULL;
	list_elem_tp elm = list_search_internal(haystack, needle, isequal);

	if(elm != NULL) {
		/* found it! */
		search_needle = elm->data;

		/* do the delete, updating pointers to next, prev, first, and last */
		if (elm->prev != NULL) {
			elm->prev->next = elm->next;
		} else {
			haystack->first = elm->next;
		}
		if (elm->next != NULL) {
			elm->next->prev = elm->prev;
		} else {
			haystack->last = elm->prev;
		}

		haystack->num_elems--;
		free(elm);
	}

	return search_needle;
}

/* search for needle in haystack, using isequal as comparator.
 * if isequal is NULL, uses simple == operator.
 * if found, return the item, else return NULL. */
void* list_search(list_tp haystack, void* needle, list_elem_isequal_fp isequal) {
	list_elem_tp search_elm = list_search_internal(haystack, needle, isequal);
	if(search_elm != NULL) {
		return search_elm->data;
	} else {
		return NULL;
	}
}

static list_elem_tp list_search_internal(list_tp haystack, void* needle, list_elem_isequal_fp isequal) {
	list_elem_tp search_needle = NULL;
	if(haystack != NULL) {
		list_elem_tp elm = haystack->first;
		while(elm != NULL) {
			int found = 0;
			if(isequal != NULL) {
				found = (*isequal)(needle, elm->data);
			} else {
				found = needle == elm->data;
			}

			if(found) {
				/* found it! */
				search_needle = elm;
				break;
			}
			elm = elm->next;
		}
	}
	return search_needle;
}

void list_destroy(list_tp list) {
	while(list->num_elems)
		list_pop_back(list);
	free(list);
	return;
}

list_iter_tp list_iterator_create(list_tp list) {
	list_iter_tp liter = NULL;
	if(list != NULL) {
		liter = (list_iter_tp) malloc(sizeof(list_iter_t));
		liter->next = list->first;
	}
	return liter;
}

void list_iterator_destroy(list_iter_tp liter) {
	if(liter != NULL) {
		free(liter);
	}
}

int list_iterator_hasnext(list_iter_tp liter) {
	if(liter != NULL && liter->next != NULL){
		return 1;
	}
	return 0;
}

void* list_iterator_getnext(list_iter_tp liter) {
	void* next = NULL;
	if(liter != NULL && liter->next != NULL) {
		next = liter->next->data;
		liter->next = liter->next->next;
	}
	return next;
}
