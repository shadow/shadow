/*
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


/*
 * linkedbuffer.c
 *
 * A shared buffer that is composed of several links. The buffer can be read
 * and written and guarantees it will not allow reading more than was written.
 * Its basically a linked queue that is written (and grows) at the front and
 * read (and shrinks) from the back. As data is written, new links are created
 * automatically. As data is read, old links are freed automatically.
 *
 *  Created on: Oct 8, 2010
 *      Author: jansen
 */

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "linkedbuffer.h"

/* forward declarations */
static void linkedbuffer_create_new_head(linkedbuffer_tp lbuffer);
static void linkedbuffer_destroy_old_tail(linkedbuffer_tp lbuffer);
static size_t linkedbuffer_get_available_bytes_tail(linkedbuffer_tp lbuffer);

static bufferlink_tp bufferlink_create(guint16 capacity);
static void bufferlink_destroy(bufferlink_tp link);

linkedbuffer_tp linkedbuffer_create(size_t link_capacity){
	linkedbuffer_tp lbuffer = g_malloc(sizeof(linkedbuffer_t));

	lbuffer->head = NULL;
	lbuffer->head_w_offset = 0;
	lbuffer->tail = NULL;
	lbuffer->tail_r_offset = 0;
	lbuffer->num_links = 0;
	lbuffer->length = 0;
	lbuffer->link_capacity = link_capacity;

	return lbuffer;
}

void linkedbuffer_destroy(linkedbuffer_tp lbuffer){
	if(lbuffer != NULL){
		bufferlink_tp link = lbuffer->tail;
		while(link != NULL){
			bufferlink_tp next = link->next;
			bufferlink_destroy(link);
			link = next;
		}
		free(lbuffer);
	} else {
		/* TODO log null pointer exception */
	}

	return;
}

size_t linkedbuffer_read(linkedbuffer_tp lbuffer, gpointer dest, size_t numbytes){
	size_t bytes_left = numbytes;
	guint32 dest_offset = 0;

	/* destroys old buffer tails proactively as opposed to lazily */

	if(lbuffer != NULL && lbuffer->tail != NULL){
		/* we will need to copy data from lbuffer to dest */
		while(bytes_left > 0 && lbuffer->tail != NULL) {
			size_t tail_avail = linkedbuffer_get_available_bytes_tail(lbuffer);

			/* if we have nothing to read, destroy old tail
			 * this *should* never happen since we destroy tails proactively
			 * but i'm leaving it in for safety
			 */
			if(tail_avail <= 0){
				linkedbuffer_destroy_old_tail(lbuffer);
				continue;
			}

			/* how much we actually read */
			size_t numread = (bytes_left < tail_avail ? bytes_left : tail_avail);
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
			tail_avail = linkedbuffer_get_available_bytes_tail(lbuffer);
			if(tail_avail <= 0 || lbuffer->length == 0){
				linkedbuffer_destroy_old_tail(lbuffer);
			}
		}

	} else {
		/* TODO log null pointer exception */
	}

	return numbytes - bytes_left;
}

size_t linkedbuffer_write(linkedbuffer_tp lbuffer, const gpointer src, size_t numbytes){
	size_t bytes_left = numbytes;
	guint32 src_offset = 0;

	/* creates new buffer heads lazily as opposed to proactively */

	if(lbuffer != NULL){

		if(lbuffer->head == NULL){
			linkedbuffer_create_new_head(lbuffer);
		}

		/* we will need to copy data from src to lbuffer */
		while(bytes_left > 0) {
			size_t head_space = lbuffer->head->capacity - lbuffer->head_w_offset;

			/* if we have no space, allocate a new link at head for more data */
			if(head_space <= 0){
				linkedbuffer_create_new_head(lbuffer);
				continue;
			}

			/* how much we actually write */
			size_t numwrite = (bytes_left < head_space ? bytes_left : head_space);
			memcpy(lbuffer->head->buf + lbuffer->head_w_offset,
					src + src_offset, numwrite);

			/* update offsets */
			src_offset += numwrite;
			lbuffer->head_w_offset += numwrite;

			/* update counts */
			bytes_left -= numwrite;
			lbuffer->length += numwrite;
		}

	} else {
		/* TODO log null pointer exception */
	}

	return numbytes - bytes_left;
}

static void linkedbuffer_create_new_head(linkedbuffer_tp lbuffer) {
	if(lbuffer->head == NULL) {
		lbuffer->head = lbuffer->tail = bufferlink_create(lbuffer->link_capacity);
		lbuffer->tail_r_offset = 0;
	} else {
		bufferlink_tp newhead = bufferlink_create(lbuffer->link_capacity);
		lbuffer->head->next = newhead;
		lbuffer->head = newhead;
	}
	lbuffer->head_w_offset = 0;
	lbuffer->num_links++;
}

static void linkedbuffer_destroy_old_tail(linkedbuffer_tp lbuffer) {
	/* if lbuffer is empty, newtail will be NULL */
	bufferlink_tp newtail = lbuffer->tail->next;
	bufferlink_destroy(lbuffer->tail);
	lbuffer->tail = newtail;
	lbuffer->tail_r_offset = 0;
	lbuffer->num_links--;

	/* if lbuffer is empty, then head was also just destroyed */
	if(lbuffer->tail == NULL){
		lbuffer->head = NULL;
		lbuffer->head_w_offset = 0;
	}
}

static size_t linkedbuffer_get_available_bytes_tail(linkedbuffer_tp lbuffer) {
	size_t bytes_available = 0;
	if(lbuffer->head == lbuffer->tail){
		bytes_available = lbuffer->head_w_offset - lbuffer->tail_r_offset;
	} else {
		bytes_available = lbuffer->tail->capacity - lbuffer->tail_r_offset;
	}
	return bytes_available;
}

static bufferlink_tp bufferlink_create(guint16 capacity){
	bufferlink_tp link = g_malloc(sizeof(bufferlink_t));

	link->buf = g_malloc(capacity);

	link->capacity = capacity;
	link->next = NULL;

	return link;
}

static void bufferlink_destroy(bufferlink_tp link){
	if(link != NULL){
		if(link->buf != NULL){
			free(link->buf);
		}
		link->capacity = 0;
		link->next = NULL;
		free(link);
	} else {
		/* TODO log null pointer exception */
	}

	return;
}
