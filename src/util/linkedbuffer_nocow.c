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


/*
 * linkedbuffer_nocow_nocow.c
 *
 * A version of linkedbuffer that does not do copy-on-write
 *
 * A data buffer (queue) that is composed of several links. The buffer can be read
 * and written and guarantees it will not allow reading more than was written.
 * Its basically a linked queue that is written (and grows) at the front and
 * read (and shrinks) from the back. Data is 'written' by passing in pointers
 * to buffers, which the lnkedbuffer will take ownership of without copying.
 * As data is read, data is copied to callers buffer while automatically freeing
 * old buffers passed in on previous calls of write.
 *
 *  Created on: Oct 27, 2010
 *      Author: jansen
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "linkedbuffer_nocow.h"
#include "utility.h"

/* forward declarations */
static void linkedbuffer_nocow_create_new_head(linkedbuffer_nocow_tp lbuffer, void* src, size_t numbytes);
static void linkedbuffer_nocow_destroy_old_tail(linkedbuffer_nocow_tp lbuffer);

static bufferlink_nocow_tp bufferlink_nocow_create(void* src, size_t numbytes);
static void bufferlink_nocow_destroy(bufferlink_nocow_tp link);

linkedbuffer_nocow_tp linkedbuffer_nocow_create(){
	linkedbuffer_nocow_tp lbuffer = malloc(sizeof(linkedbuffer_nocow_t));
	if(lbuffer == NULL){
		printf("Out of memory: linkedbuffer_nocow_create\n");
		exit(1);
	}

	lbuffer->head = NULL;
	lbuffer->tail = NULL;
	lbuffer->tail_r_offset = 0;
	lbuffer->num_links = 0;
	lbuffer->length = 0;

	return lbuffer;
}

void linkedbuffer_nocow_destroy(linkedbuffer_nocow_tp lbuffer){
	if(lbuffer != NULL){
		bufferlink_nocow_tp link = lbuffer->tail;
		while(link != NULL){
			bufferlink_nocow_tp next = link->next;
			bufferlink_nocow_destroy(link);
			link = next;
		}
	} else {
		/* TODO log null pointer exception */
	}

	free(lbuffer);
	lbuffer = NULL;

	return;
}

size_t linkedbuffer_nocow_read(linkedbuffer_nocow_tp lbuffer, void* dest, size_t numbytes){
	size_t bytes_left = numbytes;
	uint32_t dest_offset = 0;

	/* destroys old buffer tails proactively as opposed to lazily */

	if(lbuffer != NULL && lbuffer->tail != NULL){
		/* we will need to copy data from lbuffer to dest */
		while(bytes_left > 0 && lbuffer->tail != NULL) {
			size_t tail_avail = lbuffer->tail->capacity - lbuffer->tail_r_offset;

			/* how much we actually read */
			size_t numread = MIN(bytes_left, tail_avail);
			memcpy(dest + dest_offset,
					lbuffer->tail->buf + lbuffer->tail_r_offset,
					numread);

			/* update offsets */
			dest_offset += numread;
			lbuffer->tail_r_offset += numread;

			/* update counts */
			bytes_left -= numread;
			lbuffer->length -= numread;

			/* proactively destroy old tail */
			tail_avail = lbuffer->tail->capacity - lbuffer->tail_r_offset;
			if(tail_avail <= 0 || lbuffer->length == 0){
				linkedbuffer_nocow_destroy_old_tail(lbuffer);
			}
		}

	} else {
		/* TODO log null pointer exception */
	}

	return numbytes - bytes_left;
}

size_t linkedbuffer_nocow_write(linkedbuffer_nocow_tp lbuffer, void* src, size_t numbytes){
	/* add src buffer as the new head */
	linkedbuffer_nocow_create_new_head(lbuffer, src, numbytes);
	lbuffer->length += numbytes;
	return numbytes;
}

static void linkedbuffer_nocow_create_new_head(linkedbuffer_nocow_tp lbuffer, void* src, size_t numbytes) {
	bufferlink_nocow_tp newhead = bufferlink_nocow_create(src, numbytes);
	if(lbuffer->head == NULL) {
		lbuffer->head = lbuffer->tail = newhead;
		lbuffer->tail_r_offset = 0;
	} else {
		lbuffer->head->next = newhead;
		lbuffer->head = newhead;
	}
	lbuffer->num_links++;
}

static void linkedbuffer_nocow_destroy_old_tail(linkedbuffer_nocow_tp lbuffer) {
	/* if lbuffer is empty, newtail will be NULL */
	bufferlink_nocow_tp newtail = lbuffer->tail->next;
	bufferlink_nocow_destroy(lbuffer->tail);
	lbuffer->tail = newtail;
	lbuffer->tail_r_offset = 0;
	lbuffer->num_links--;

	/* if lbuffer is empty, then head was also just destroyed */
	if(lbuffer->tail == NULL){
		lbuffer->head = NULL;
	}
}

static bufferlink_nocow_tp bufferlink_nocow_create(void* src, size_t numbytes){
	bufferlink_nocow_tp link = malloc(sizeof(bufferlink_nocow_t));
	if(link == NULL){
		printf("Out of memory: bufferlink_nocow_create\n");
		exit(1);
	}

	link->buf = src;
	link->capacity = numbytes;
	link->next = NULL;

	return link;
}

static void bufferlink_nocow_destroy(bufferlink_nocow_tp link){
	if(link != NULL){
		if(link->buf != NULL){
			free(link->buf);
		}
		link->capacity = 0;
		link->next = NULL;
	} else {
		/* TODO log null pointer exception */
	}

	free(link);
	link = NULL;

	return;
}
