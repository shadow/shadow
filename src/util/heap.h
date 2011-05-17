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

#ifndef _heap_h
#define _heap_h

#define itemswap(a,b,tmp) do{ tmp=a; a=b; b=tmp; }while(0)

/**
 *  a straightforward heap implementation
 *
 * 	this is a MAXHEAP. if you want to use it as a minheap, simple negate the return value
 *  of your supplied compare function
 *
 */


typedef struct HEAP {
	void ** heap;
	unsigned int ptr;
	unsigned int heapsize;
	unsigned int default_size;
	int (*compare)(void *a, void *b);
} * heap_tp;

/**
 * creates a heap!
 */
heap_tp heap_create (int (*compare)(void *a, void *b), unsigned int default_size);

/**
 * destroys the heap
 *
 */
void heap_destroy(heap_tp);

/**
 * deletes the item at index i from the heap
 */
void * heap_remove(heap_tp heap, unsigned int i);
/**
 * return the number of elements currently in the heap
 */
unsigned int heap_getsize(heap_tp heap);

/**
 * returns the item in the heap at index i
 */
void * heap_get(heap_tp heap, unsigned int i);

/**
 * inserts an item o into the heap
 */
int heap_insert(heap_tp heap, void * o);

#endif

