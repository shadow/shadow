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

#include "shadow.h"

struct _Channel {
	Transport super;

	enum ChannelType type;

	gint linkedHandle;

	ByteQueue* buffer;
	gsize bufferSize;
	gsize bufferLength;

	MAGIC_DECLARE;
};

static void channel_close(Channel* channel) {
	MAGIC_ASSERT(channel);
	descriptor_adjustStatus((Descriptor*)channel, DS_CLOSED, TRUE);
	node_closeDescriptor(worker_getPrivate()->cached_node, channel->super.super.handle);
}

static void channel_free(Channel* channel) {
	MAGIC_ASSERT(channel);

	bytequeue_free(channel->buffer);

	MAGIC_CLEAR(channel);
	g_free(channel);
}

static gssize channel_linkedWrite(Channel* channel, gconstpointer buffer, gsize nBytes) {
	MAGIC_ASSERT(channel);
	/* our linked channel is trying to send us data, make sure we can read it */
	g_assert(!(channel->type & CT_WRITEONLY));

	gsize available = channel->bufferSize - channel->bufferLength;
	if(available == 0) {
		/* we have no space */
		return -1;
	}

	/* accept some data from the other end of the pipe */
	guint copyLength = (guint) MIN(nBytes, available);
	bytequeue_push(channel->buffer, buffer, copyLength);
	channel->bufferLength += copyLength;

	/* we just got some data in our buffer */
	descriptor_adjustStatus((Descriptor*)channel, DS_READABLE, TRUE);

	return copyLength;
}

static Channel* channel_getLinkedChannel(Channel* channel) {
	MAGIC_ASSERT(channel);
	return (Channel*)node_lookupDescriptor(worker_getPrivate()->cached_node, channel->linkedHandle);
}

static gssize channel_sendUserData(Channel* channel, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(channel);
	/* the read end of a unidirectional pipe can not write! */
	g_assert(channel->type != CT_READONLY);

	gint result = 0;

	Channel* linkedChannel = channel_getLinkedChannel(channel);
	if(linkedChannel) {
		result = channel_linkedWrite(linkedChannel, buffer, nBytes);
	}

	/* our end cant write anymore if they returned error */
	if(result <= 0) {
		descriptor_adjustStatus((Descriptor*)channel, DS_WRITABLE, FALSE);
	}

	return result;
}

static gssize channel_receiveUserData(Channel* channel, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(channel);
	/* the write end of a unidirectional pipe can not read! */
	g_assert(channel->type != CT_WRITEONLY);

	gsize available = channel->bufferLength;
	if(available == 0) {
		/* we have no data */
		if(!channel_getLinkedChannel(channel)) {
			/* the other end closed (EOF) */
			return 0;
		} else {
			/* blocking on read */
			return -1;
		}
	}

	/* accept some data from the other end of the pipe */
	guint copyLength = (guint) MIN(nBytes, available);
	bytequeue_pop(channel->buffer, buffer, copyLength);
	channel->bufferLength -= copyLength;

	/* we are no longer readable if we have nothing left */
	if(channel->bufferLength <= 0) {
		descriptor_adjustStatus((Descriptor*)channel, DS_READABLE, FALSE);
	}

	return copyLength;
}

TransportFunctionTable channel_functions = {
	(DescriptorFunc) channel_close,
	(DescriptorFunc) channel_free,
	(TransportSendFunc) channel_sendUserData,
	(TransportReceiveFunc) channel_receiveUserData,
	MAGIC_VALUE
};

Channel* channel_new(gint handle, gint linkedHandle, enum ChannelType type) {
	Channel* channel = g_new0(Channel, 1);
	MAGIC_INIT(channel);

	transport_init(&(channel->super), &channel_functions, DT_PIPE, handle);

	channel->type = type;
	channel->buffer = bytequeue_new(8192);
	channel->bufferSize = CONFIG_PIPE_BUFFER_SIZE;
	channel->linkedHandle = linkedHandle;

	descriptor_adjustStatus((Descriptor*)channel, DS_ACTIVE, TRUE);
	if(!(type & CT_READONLY)) {
		descriptor_adjustStatus((Descriptor*)channel, DS_WRITABLE, TRUE);
	}

	return channel;
}

gint channel_getLinkedHandle(Channel* channel) {
	MAGIC_ASSERT(channel);
	return channel->linkedHandle;
}
