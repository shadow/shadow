/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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


#ifndef SHD_BYTE_QUEUE_H_
#define SHD_BYTE_QUEUE_H_

#include <glib.h>
#include <stdint.h>
#include <stddef.h>

/**
 * A shared buffer that is composed of several chunks. The buffer can be read
 * and written and guarantees it will not allow reading more than was written.
 * Its basically a linked queue that is written (and grows) at the front and
 * read (and shrinks) from the back. As data is written, new chunks are created
 * automatically. As data is read, old chunks are freed automatically.
 */

typedef struct _ByteQueue ByteQueue;

ByteQueue* bytequeue_new(gsize chunkSize);
void bytequeue_free(ByteQueue* bqueue);
gsize bytequeue_pop(ByteQueue* bqueue, gpointer outBuffer, gsize nBytes);
gsize bytequeue_push(ByteQueue* bqueue, gconstpointer inputBuffer, gsize nBytes);

#endif /* SHD_BYTE_QUEUE_H_ */
