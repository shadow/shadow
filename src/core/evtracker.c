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
#include "evtracker.h"
#include "hash.h"

evtracker_tp evtracker_create (size_t buf_size, unsigned int granularity) {
	evtracker_tp evt = malloc(sizeof(*evt));
	if(!evt)
		printfault(EXIT_NOMEM, "evtracker_create: Out of memory.");

	if(buf_size == 0)
		printfault(EXIT_UNKNOWN, "evtracker_create: Invalid buffersize.");

	evt->evhash = calloc(buf_size, sizeof(*evt->evhash));
	if(!evt->evhash)
		printfault(EXIT_NOMEM, "evtracker_create: Out of memory.");

	evt->evheap = heap_create(evtracker_heap_e_compare, EVTRACKER_HEAP_DEAFAULTSIZE);
	evt->last_accessed_time = PTIME_INVALID;
	evt->last_hash_e = NULL;
	evt->size = buf_size;
	evt->num_events = 0;
	evt->granularity = granularity;

	return evt;
}

int evtracker_heap_e_compare(void * a, void * b) {
	struct EVTRACKER_HEAP_E * a_e = (struct EVTRACKER_HEAP_E *)a;
	struct EVTRACKER_HEAP_E * b_e = (struct EVTRACKER_HEAP_E *)b;
	return (a_e->time == b_e->time ? 0 : (a_e->time > b_e->time ? -1 : 1));
	//ptimer_eq(&a_e->time, &b_e->time) ? 0 : ( ptimer_cmp(&a_e->time, &b_e->time, >) ? -1 : 1 );
}

void evtracker_destroy(evtracker_tp evt) {
	struct EVTRACKER_HEAP_E * heape = heap_remove(evt->evheap,0);

	while(heape) {
		free(heape->hash_e->data);
		free(heape->hash_e);
		free(heape);
		heape = heap_remove(evt->evheap,0);
	}

	heap_destroy(evt->evheap);
	free(evt->evhash);
	free(evt);
	return;
}

struct EVTRACKER_HASH_E * evtracker_find_hash_e(evtracker_tp evt, ptime_t time, char create_okay) {
	struct EVTRACKER_HASH_E * hashe = NULL;
	struct EVTRACKER_HASH_E ** tmp = NULL;
	struct EVTRACKER_HASH_E * p = NULL;
	int hash_idx = time % evt->size;

	if(evt->last_hash_e != NULL && evt->last_accessed_time == time)
		hashe = evt->last_hash_e;
	else {
		/* find it */
		hashe = evt->evhash[hash_idx];

		if(!hashe && create_okay)
			tmp = &(evt->evhash[hash_idx]);
		else {
			while(hashe != NULL && hashe->time != time) {
				if(time < hashe->time)
					tmp = &hashe->l;
				else
					tmp = &hashe->r;

				p = hashe;
				hashe = *tmp;
			}
		}
	}

	if(hashe == NULL && create_okay) {
		struct EVTRACKER_HEAP_E * heape;

		*tmp = malloc(sizeof(struct EVTRACKER_HASH_E));

		if(!*tmp)
			printfault(EXIT_NOMEM, "evtracker_find_hash_e: Out of memory");

		(*tmp)->data = NULL;
		(*tmp)->data_size = 0;
		(*tmp)->data_write_ptr = (*tmp)->data_read_ptr = 0;
		(*tmp)->l = (*tmp)->r = NULL;
		(*tmp)->p = p;
		(*tmp)->time = time;
		hashe = (*tmp); //evt->evhash[hash_idx] = (*tmp);

		heape = malloc(sizeof(*heape));
		if(!heape)
			printfault(EXIT_NOMEM, "evtracker_find_hash_e: Out of memory");

		heape->hash_e = hashe;
		heape->hash_offset= hash_idx;
		heape->time = time;

		/* add it to the heap */
		heap_insert(evt->evheap, heape);
	}

	return hashe;
}

void evtracker_insert_event(evtracker_tp evt, ptime_t time, void * data) {
	struct EVTRACKER_HASH_E * hashe;
	unsigned int sliced;

	/* modify the time for the given granularity */
	if(time != PTIME_INVALID) {
		sliced = time % evt->granularity;
		if(sliced)
			time = time + evt->granularity - sliced;
	}

	/* find the hash element */
	hashe = evtracker_find_hash_e(evt, time, 1);

	if(hashe->data == NULL) {
		hashe->data = malloc(EVTRACKER_DATASTORE_DEFAULTSIZE * sizeof(*hashe->data));
		if(!hashe->data)
			printfault(EXIT_NOMEM, "evtracker_insert_event: Out of memory");

		hashe->data_size = EVTRACKER_DATASTORE_DEFAULTSIZE;
		hashe->data_read_ptr = hashe->data_write_ptr = 0;
	} else if(hashe->data_write_ptr == hashe->data_size) {
		hashe->data_size *= 2;
		hashe->data = realloc(hashe->data, hashe->data_size * sizeof(*hashe->data));
		if(!hashe->data)
			printfault(EXIT_NOMEM, "evtracker_insert_event: Out of memory");
	}

	hashe->data[hashe->data_write_ptr++] = data;
	evt->num_events++;
	evt->last_accessed_time = time;
	evt->last_hash_e = hashe;

	return;
}

unsigned int evtracker_get_numevents(evtracker_tp evt) {
	return evt->num_events;
}

void * evtracker_get_nextevent(evtracker_tp evt, ptime_t * time, char removal) {
	void * rv = NULL;
	struct EVTRACKER_HEAP_E * heape = heap_get(evt->evheap,0);

	if(heape != NULL && heape->hash_e->data_read_ptr < heape->hash_e->data_write_ptr) {
		rv = heape->hash_e->data[heape->hash_e->data_read_ptr];
		if(time != NULL)
			*time = heape->time;

		if(removal) {
			heape->hash_e->data_read_ptr++;
			evt->num_events--;
		}

		if(heape->hash_e->data_read_ptr == heape->hash_e->data_write_ptr) {
			struct EVTRACKER_HASH_E * hashe = heape->hash_e, ** rep = NULL, * rep2 = NULL;

			/* we have cleanup to do -- this hash entry is now empty. heap removal is simple. */
			heap_remove(evt->evheap, 0);
			free(heape);

			/* find the replacement for the current node in the btree */
			if(hashe->l != NULL){
				if(hashe->r != NULL) {
					rep = &hashe->l;
					while((*rep)->r)
						rep = &((*rep)->r);
				} else
					rep2 = hashe->l;
			} else if(hashe->r != NULL)
				rep2 = hashe->r;

			/* full replacement */
			if(rep != NULL) {
				rep2 = *rep;
				*rep = rep2->l; /* parent of rep has new correct child */
				if(rep2->l)
					rep2->l->p = rep2->p; /* correct child now has previous parent of rep */

				rep2->l = hashe->l;
				rep2->r = hashe->r;
			}

			if(rep2 != NULL)
				rep2->p = hashe->p;

			if(hashe->p) {
				if(hashe->p->r == hashe)
					hashe->p->r = rep2;
				else
					hashe->p->l = rep2;
			} else
				evt->evhash[hashe->time % evt->size] = rep2;

			/* hooray for ram */
			free(hashe->data);
			free(hashe);
		}
	}

	return rv;
}

ptime_t evtracker_earliest_event(evtracker_tp evt, ptime_t * maximum) {
	ptime_t pt = PTIME_INVALID;
	if(evtracker_get_nextevent(evt, &pt, 0)) {
		if(pt != PTIME_INVALID && maximum && *maximum != PTIME_INVALID && *maximum < pt)
			pt = *maximum;
	} else if(maximum)
		pt = *maximum;

	return pt;
}

