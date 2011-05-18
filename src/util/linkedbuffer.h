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


#ifndef LINKEDBUFFER_H_
#define LINKEDBUFFER_H_

#include <stdint.h>
#include <stddef.h>

typedef struct bufferlink_t{
	void* buf;
	uint16_t capacity;
	struct bufferlink_t* next;
}bufferlink_t, *bufferlink_tp;

typedef struct linkedbuffer{
	bufferlink_tp tail;
	uint16_t tail_r_offset;
	bufferlink_tp head;
	uint16_t head_w_offset;
	uint16_t num_links;
	size_t length;
	size_t link_capacity;
}linkedbuffer_t, *linkedbuffer_tp;

linkedbuffer_tp linkedbuffer_create(size_t link_capacity);
void linkedbuffer_destroy(linkedbuffer_tp lbuffer);
size_t linkedbuffer_read(linkedbuffer_tp lbuffer, void* dest, size_t numbytes);
size_t linkedbuffer_write(linkedbuffer_tp lbuffer, const void* src, size_t numbytes);

#endif /* LINKEDBUFFER_H_ */
