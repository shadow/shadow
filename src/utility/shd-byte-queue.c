/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "shd-utility.h"
#include "shd-byte-queue.h"

typedef struct _ByteChunk ByteChunk;
struct _ByteChunk {
	gpointer buf;
	gsize capacity;
	ByteChunk* next;
};

struct _ByteQueue {
	ByteChunk* tail;
	gsize tail_r_offset;
	ByteChunk* head;
	gsize head_w_offset;
	gsize num_chunks;
	gsize length;
	gsize chunk_capacity;
};

static ByteChunk* bytechunk_new(gsize chunkSize){
	ByteChunk* chunk = g_new0(ByteChunk, 1);

	chunk->buf = g_malloc(chunkSize);

	chunk->capacity = chunkSize;
	chunk->next = NULL;

	return chunk;
}

static void bytechunk_free(ByteChunk* chunk){
	utility_assert(chunk);

	if(chunk->buf != NULL){
		g_free(chunk->buf);
	}
	chunk->capacity = 0;
	chunk->next = NULL;
	g_free(chunk);

	return;
}

static void bytequeue_create_new_head(ByteQueue* bqueue) {
	if(bqueue->head == NULL) {
		bqueue->head = bqueue->tail = bytechunk_new(bqueue->chunk_capacity);
		bqueue->tail_r_offset = 0;
	} else {
		ByteChunk* newhead = bytechunk_new(bqueue->chunk_capacity);
		bqueue->head->next = newhead;
		bqueue->head = newhead;
	}
	bqueue->head_w_offset = 0;
	bqueue->num_chunks++;
}

static void bytequeue_destroy_old_tail(ByteQueue* bqueue) {
	/* if bqueue is empty, newtail will be NULL */
	ByteChunk* newtail = bqueue->tail->next;
	bytechunk_free(bqueue->tail);
	bqueue->tail = newtail;
	bqueue->tail_r_offset = 0;
	bqueue->num_chunks--;

	/* if bqueue is empty, then head was also just destroyed */
	if(bqueue->tail == NULL){
		bqueue->head = NULL;
		bqueue->head_w_offset = 0;
	}
}

static gsize bytequeue_get_available_bytes_tail(ByteQueue* bqueue) {
	gsize bytes_available = 0;
	if(bqueue->head == bqueue->tail){
		bytes_available = bqueue->head_w_offset - bqueue->tail_r_offset;
	} else {
		bytes_available = bqueue->tail->capacity - bqueue->tail_r_offset;
	}
	return bytes_available;
}

ByteQueue* bytequeue_new(gsize chunkSize){
	ByteQueue* bqueue = g_new0(ByteQueue, 1);

	bqueue->head = NULL;
	bqueue->head_w_offset = 0;
	bqueue->tail = NULL;
	bqueue->tail_r_offset = 0;
	bqueue->num_chunks = 0;
	bqueue->length = 0;
	bqueue->chunk_capacity = chunkSize;

	return bqueue;
}

void bytequeue_free(ByteQueue* bqueue){
	utility_assert(bqueue);

	ByteChunk* chunk = bqueue->tail;
	while(chunk != NULL){
		ByteChunk* next = chunk->next;
		bytechunk_free(chunk);
		chunk = next;
	}
	g_free(bqueue);

	return;
}

gsize bytequeue_pop(ByteQueue* bqueue, gpointer outBuffer, gsize nBytes){
	utility_assert(bqueue && outBuffer);
	gsize bytes_left = nBytes;
	guint32 dest_offset = 0;

	/* destroys old buffer tails proactively as opposed to lazily */

	if(bqueue->tail != NULL){
		/* we will need to copy data from bqueue to outBuffer */
		while(bytes_left > 0 && bqueue->tail != NULL) {
			gsize tail_avail = bytequeue_get_available_bytes_tail(bqueue);

			/* if we have nothing to read, destroy old tail
			 * this *should* never happen since we destroy tails proactively
			 * but i'm leaving it in for safety
			 */
			if(tail_avail <= 0){
				bytequeue_destroy_old_tail(bqueue);
				continue;
			}

			/* how much we actually read */
			gsize numread = (bytes_left < tail_avail ? bytes_left : tail_avail);
			memcpy(outBuffer + dest_offset,
					bqueue->tail->buf + bqueue->tail_r_offset,
					numread);

			/* update offsets */
			dest_offset += numread;
			bqueue->tail_r_offset += numread;

			/* update counts */
			bytes_left -= numread;
			bqueue->length -= numread;

			/* proactively destroy old tail */
			tail_avail = bytequeue_get_available_bytes_tail(bqueue);
			if(tail_avail <= 0 || bqueue->length == 0){
				bytequeue_destroy_old_tail(bqueue);
			}
		}

	}

	return nBytes - bytes_left;
}

gsize bytequeue_push(ByteQueue* bqueue, gconstpointer inputBuffer, gsize nBytes){
	utility_assert(bqueue && inputBuffer);
	gsize bytes_left = nBytes;
	guint32 src_offset = 0;

	/* creates new buffer heads lazily as opposed to proactively */

	if(bqueue->head == NULL){
		bytequeue_create_new_head(bqueue);
	}

	/* we will need to copy data from src to bqueue */
	while(bytes_left > 0) {
		gsize head_space = bqueue->head->capacity - bqueue->head_w_offset;

		/* if we have no space, allocate a new chunk at head for more data */
		if(head_space <= 0){
			bytequeue_create_new_head(bqueue);
			continue;
		}

		/* how much we actually write */
		gsize numwrite = (bytes_left < head_space ? bytes_left : head_space);
		memcpy(bqueue->head->buf + bqueue->head_w_offset,
				inputBuffer + src_offset, numwrite);

		/* update offsets */
		src_offset += numwrite;
		bqueue->head_w_offset += numwrite;

		/* update counts */
		bytes_left -= numwrite;
		bqueue->length += numwrite;
	}

	return nBytes - bytes_left;
}
