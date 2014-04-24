/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
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
	host_closeDescriptor(worker_getCurrentHost(), channel->super.super.handle);
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
	utility_assert(!(channel->type & CT_WRITEONLY));

	gsize available = channel->bufferSize - channel->bufferLength;
	if(available == 0) {
		/* we have no space */
		return (gssize)-1;
	}

	/* accept some data from the other end of the pipe */
	gsize copyLength = MIN(nBytes, available);
	gsize numCopied = bytequeue_push(channel->buffer, buffer, copyLength);
	channel->bufferLength += numCopied;

	/* we just got some data in our buffer */
	descriptor_adjustStatus((Descriptor*)channel, DS_READABLE, TRUE);

	return (gssize)numCopied;
}

static Channel* channel_getLinkedChannel(Channel* channel) {
	MAGIC_ASSERT(channel);
	return (Channel*)host_lookupDescriptor(worker_getCurrentHost(), channel->linkedHandle);
}

static gssize channel_sendUserData(Channel* channel, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
	MAGIC_ASSERT(channel);
	/* the read end of a unidirectional pipe can not write! */
	utility_assert(channel->type != CT_READONLY);

	gssize result = 0;

	Channel* linkedChannel = channel_getLinkedChannel(channel);
	if(linkedChannel) {
		result = channel_linkedWrite(linkedChannel, buffer, nBytes);
	}

	/* our end cant write anymore if they returned error */
	if(result <= (gssize)0) {
		descriptor_adjustStatus((Descriptor*)channel, DS_WRITABLE, FALSE);
	}

	return result;
}

static gssize channel_receiveUserData(Channel* channel, gpointer buffer, gsize nBytes, in_addr_t* ip, in_port_t* port) {
	MAGIC_ASSERT(channel);
	/* the write end of a unidirectional pipe can not read! */
	utility_assert(channel->type != CT_WRITEONLY);

	gsize available = channel->bufferLength;
	if(available == 0) {
		/* we have no data */
		if(!channel_getLinkedChannel(channel)) {
			/* the other end closed (EOF) */
			return (gssize)0;
		} else {
			/* blocking on read */
			return (gssize)-1;
		}
	}

	/* accept some data from the other end of the pipe */
	gsize copyLength = MIN(nBytes, available);
	gsize numCopied = bytequeue_pop(channel->buffer, buffer, copyLength);
	channel->bufferLength -= numCopied;

	/* we are no longer readable if we have nothing left */
	if(channel->bufferLength <= 0) {
		descriptor_adjustStatus((Descriptor*)channel, DS_READABLE, FALSE);
	}

	return (gssize)numCopied;
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
