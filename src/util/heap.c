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
#include "heap.h"

heap_tp heap_create (int (*compare)(void *a, void *b), unsigned int default_size) {
	heap_tp rval;

	if(default_size == 0)
		return NULL;

	rval = (heap_tp)malloc(sizeof(struct HEAP));
	if(!rval)
		return NULL;

	rval->default_size = default_size;
	rval->heap = malloc(sizeof(void*) * default_size);
	rval->heapsize = default_size;
	rval->ptr = 0;
	rval->compare = compare;

	return rval;
}

void heap_destroy(heap_tp heap) {
	unsigned int i;

	if(heap) {
		if(heap->heap) {
			for(i=0; i<heap->ptr; i++){
				if(heap->heap[i]!=NULL)
					free(heap->heap[i]);
			}

			free(heap->heap);
		}
		free(heap);
	}
	return;
}

/**
 * deletes the item at index i from the heap
 */
void * heap_remove(heap_tp heap, unsigned int i) {
	unsigned int childa, childb;
	void * rv = NULL;
	unsigned int j;
	void * tmp;

	if(heap->ptr == 0);
	else if(heap->ptr == 1 || i == (heap->ptr - 1)) {
		heap->ptr--;
		rv = heap->heap[i];
	} else {
		rv = heap->heap[i];
		heap->heap[i] = heap->heap[--heap->ptr];

		for(j=i; j<heap->ptr;) {
			childa = 2*j+1;
			childb = 2*j+2;

			if(childb < heap->ptr) {
				if((*heap->compare)(heap->heap[j], heap->heap[childb]) < 0) {
					if((*heap->compare)(heap->heap[childa], heap->heap[childb]) < 0) {
						/* swap j <-> childb */
						itemswap(heap->heap[childb], heap->heap[j],tmp);
						j = childb;
					} else {
						/* swap j <-> childa */
						itemswap(heap->heap[childa], heap->heap[j],tmp);
						j = childa;
					}
				} else if((*heap->compare)(heap->heap[j], heap->heap[childa]) < 0) {
					/* swap j <-> childa */
					itemswap(heap->heap[childa], heap->heap[j],tmp);
					j = childa;
				} else
					break;
			} else if(childa < heap->ptr && (*heap->compare)(heap->heap[j], heap->heap[childa]) < 0) {
				/* swap j <-> childa */
				itemswap(heap->heap[childa], heap->heap[j],tmp);
				j = childa;
			} else {
				break;
			}
		}
	}

	/* shrink the heapsize if possible */
	if(heap->ptr < (heap->heapsize/2) && (heap->heapsize/2) >= heap->default_size) {
		heap->heapsize = heap->heapsize >> 1;
		heap->heap = realloc((void*)heap->heap, heap->heapsize * sizeof(void*));
	}

	return rv;
}

/**
 * return the number of elements currently in the heap
 */
unsigned int heap_getsize(heap_tp heap) {
	return heap->ptr;
}

/**
 * returns the item in the heap at index i
 */
void * heap_get(heap_tp heap, unsigned int i) {
	if(i < heap->ptr)
		return heap->heap[i];
	else
		return NULL;
}

/**
 * inserts an item o into the heap
 */
int heap_insert(heap_tp heap, void * o) {
	unsigned int i, parent;
	void * tmp;
	if(!o)
		return 0;

	if(heap->ptr == heap->heapsize) {
		heap->heapsize *= 2;
		heap->heap = realloc(heap->heap, heap->heapsize * sizeof(void*));
	}

	i = heap->ptr;
	heap->heap[heap->ptr++] = o;

	while(i != 0) {
		parent = (i-1)/2;

		if((*heap->compare)(heap->heap[i], heap->heap[parent]) > 0) {
			itemswap(heap->heap[i], heap->heap[parent], tmp);
			i = parent;
		} else
			break;
	}

	return 1;
}

