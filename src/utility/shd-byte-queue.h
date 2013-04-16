/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
