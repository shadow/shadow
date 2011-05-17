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


#ifndef linkedbuffer_nocow_NOCOW_H_
#define linkedbuffer_nocow_NOCOW_H_

#include <stdint.h>
#include <stddef.h>

#define MIN(a,b) ((a)<(b)?(a):(b))

typedef struct bufferlink_nocow_t{
	void* buf;
	size_t capacity;
	struct bufferlink_nocow_t* next;
}bufferlink_nocow_t, *bufferlink_nocow_tp;

typedef struct linkedbuffer_nocow_t{
	bufferlink_nocow_tp tail;
	size_t tail_r_offset;
	bufferlink_nocow_tp head;
	size_t num_links;
	size_t length;
}linkedbuffer_nocow_t, *linkedbuffer_nocow_tp;

/* creates a new linkedbuffer that is initially empty */
linkedbuffer_nocow_tp linkedbuffer_nocow_create();
/* destroy the given linked buffer, freeing all memory stored by links */
void linkedbuffer_nocow_destroy(linkedbuffer_nocow_tp lbuffer);
/* copies numbytes bytes of data from the queue to the dest buffer */
size_t linkedbuffer_nocow_read(linkedbuffer_nocow_tp lbuffer, void* dest, size_t numbytes);
/* takes ownership of src buffer, effectively adding its data to the queue.
 * the caller should set its pointers to src buffer to NULL after this call.
 */
size_t linkedbuffer_nocow_write(linkedbuffer_nocow_tp lbuffer, void* src, size_t numbytes);

#endif /* linkedbuffer_nocow_NOCOW_H_ */
