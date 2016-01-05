/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Channel {
    Transport super;

    ChannelType type;
    Channel* linkedChannel;

    ByteQueue* buffer;
    gsize bufferSize;
    gsize bufferLength;

    MAGIC_DECLARE;
};

static void channel_close(Channel* channel) {
    MAGIC_ASSERT(channel);
    /* tell our link that we are done */
    if(channel->linkedChannel) {
        if(channel == channel->linkedChannel->linkedChannel) {
            /* the link will no longer hold a ref to us */
            descriptor_unref(&channel->linkedChannel->linkedChannel->super.super);
            channel->linkedChannel->linkedChannel = NULL;
        }
        /* we will no longer hold a ref to the link */
        descriptor_unref(&channel->linkedChannel->super.super);
        channel->linkedChannel = NULL;
    }

    /* host can stop monitoring us for changes */
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

static gssize channel_sendUserData(Channel* channel, gconstpointer buffer, gsize nBytes, in_addr_t ip, in_port_t port) {
    MAGIC_ASSERT(channel);
    /* the read end of a unidirectional pipe can not write! */
    utility_assert(channel->type != CT_READONLY);

    gssize result = 0;

    if(channel->linkedChannel) {
        result = channel_linkedWrite(channel->linkedChannel, buffer, nBytes);
    } else {
        /* the other end closed or doesn't exist */
        result = -1;
        errno = EPIPE;
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
        if(!channel->linkedChannel) {
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

Channel* channel_new(gint handle, ChannelType type) {
    Channel* channel = g_new0(Channel, 1);
    MAGIC_INIT(channel);

    transport_init(&(channel->super), &channel_functions, DT_PIPE, handle);

    channel->type = type;
    channel->buffer = bytequeue_new(8192);
    channel->bufferSize = CONFIG_PIPE_BUFFER_SIZE;

    descriptor_adjustStatus((Descriptor*)channel, DS_ACTIVE, TRUE);
    if(!(type & CT_READONLY)) {
        descriptor_adjustStatus((Descriptor*)channel, DS_WRITABLE, TRUE);
    }

    return channel;
}

void channel_setLinkedChannel(Channel* channel, Channel* linkedChannel) {
    MAGIC_ASSERT(channel);
    MAGIC_ASSERT(linkedChannel);

    if(channel->linkedChannel) {
        descriptor_unref(&channel->linkedChannel->super.super);
    }
    channel->linkedChannel = linkedChannel;
    descriptor_ref(&linkedChannel->super.super);
}

Channel* channel_getLinkedChannel(Channel* channel) {
    MAGIC_ASSERT(channel);
    return channel->linkedChannel;
}
