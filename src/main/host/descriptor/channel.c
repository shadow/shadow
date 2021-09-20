/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/channel.h"

#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>

#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/transport.h"
#include "main/host/host.h"
#include "main/utility/utility.h"

struct _Channel {
    Transport super;

    ChannelType type;
    Channel* linkedChannel;

    ByteQueue* buffer;
    gsize bufferSize;
    gsize bufferLength;

    MAGIC_DECLARE;
};

static Channel* _channel_fromLegacyDescriptor(LegacyDescriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_PIPE ||
                   descriptor_getType(descriptor) == DT_UNIXSOCKET);
    return (Channel*)descriptor;
}

static void channel_close(LegacyDescriptor* descriptor, Host* host) {
    Channel* channel = _channel_fromLegacyDescriptor(descriptor);
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
}

static void channel_free(LegacyDescriptor* descriptor) {
    Channel* channel = _channel_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(channel);

    bytequeue_free(channel->buffer);

    descriptor_clear((LegacyDescriptor*)channel);
    MAGIC_CLEAR(channel);
    g_free(channel);

    worker_count_deallocation(Channel);
}

static gssize channel_linkedWrite(Channel* channel, Thread* thread, PluginVirtualPtr buffer,
                                  gsize nBytes) {
    MAGIC_ASSERT(channel);
    /* our linked channel is trying to send us data, make sure we can read it */
    utility_assert(!(channel->type & CT_WRITEONLY));

    gsize available = channel->bufferSize - channel->bufferLength;
    if(available == 0) {
        /* we have no space */
        return (gssize)-EWOULDBLOCK;
    }

    // TODO: Add an interface to bytequeue that would allow us to use
    // worker_readPtr to read directly into its storage, saving a potential copy
    // inside worker_getReadablePtr. e.g. one that accepts a read callback.
    gsize copyLength = MIN(nBytes, available);
    const void* readablePtr = process_getReadablePtr(thread_getProcess(thread), buffer, copyLength);
    if (!readablePtr) {
        return -EFAULT;
    }

    /* accept some data from the other end of the pipe */
    bytequeue_push(channel->buffer, readablePtr, copyLength);
    channel->bufferLength += copyLength;

    /* we just got some data in our buffer */
    descriptor_adjustStatus((LegacyDescriptor*)channel, STATUS_DESCRIPTOR_READABLE, TRUE);

    return copyLength;
}

static gssize channel_sendUserData(Transport* transport, Thread* thread, PluginVirtualPtr buffer,
                                   gsize nBytes, in_addr_t ip, in_port_t port) {
    Channel* channel = _channel_fromLegacyDescriptor((LegacyDescriptor*)transport);
    MAGIC_ASSERT(channel);
    /* the read end of a unidirectional pipe can not write! */
    utility_assert(channel->type != CT_READONLY);

    /* Zero-size writes of pipes aren't very clearly specified. The pipe(2)
     * documentation for O_DIRECT indicates that a size-zero write is a no-op
     * with O_DIRECT; experimentally they are also a no-op without it. */
    if (nBytes == 0) {
        return 0;
    }

    gssize result = 0;

    if(channel->linkedChannel) {
        if (nBytes > 0) {
            result = channel_linkedWrite(channel->linkedChannel, thread, buffer, nBytes);
        }
    } else {
        /* the other end closed or doesn't exist */
        result = -EPIPE;
    }

    /* our end cant write anymore if they returned error (other than the source being inaccessible) */
    if(result <= (gssize)0 && result != -EFAULT) {
        descriptor_adjustStatus((LegacyDescriptor*)channel, STATUS_DESCRIPTOR_WRITABLE, FALSE);
    }

    return result;
}

static gssize channel_receiveUserData(Transport* transport, Thread* thread, PluginVirtualPtr buffer,
                                      gsize nBytes, in_addr_t* ip, in_port_t* port) {
    Channel* channel = _channel_fromLegacyDescriptor((LegacyDescriptor*)transport);
    MAGIC_ASSERT(channel);
    /* the write end of a unidirectional pipe can not read! */
    utility_assert(channel->type != CT_WRITEONLY);

    /* Zero-size reads of pipes aren't very clearly specified, but
     * experimentally they are a no-op. */
    if (nBytes == 0) {
        return 0;
    }

    gsize available = channel->bufferLength;
    if(available == 0) {
        /* we have no data */
        if(!channel->linkedChannel) {
            /* the other end closed (EOF) */
            return (gssize)0;
        } else {
            /* blocking on read */
            return (gssize)-EWOULDBLOCK;
        }
    }

    // TODO: Use `worker_writePtr` to write directly into the process's memory,
    // saving a potential copy in worker_getWritablePtr. e.g. add an interface
    // to bytequeue that takes a `write` callback.
    gsize copyLength = MIN(nBytes, available);
    void* writableBuf = process_getWriteablePtr(thread_getProcess(thread), buffer, copyLength);
    if (!writableBuf) {
        return -EFAULT;
    }

    /* accept some data from the other end of the pipe */
    gsize numCopied = bytequeue_pop(channel->buffer, writableBuf, copyLength);
    channel->bufferLength -= numCopied;

    /* we are no longer readable if we have nothing left */
    if(channel->bufferLength <= 0) {
        descriptor_adjustStatus((LegacyDescriptor*)channel, STATUS_DESCRIPTOR_READABLE, FALSE);
    }

    /* The linked channel is now writable. */
    utility_assert(channel->linkedChannel);
    utility_assert(channel->bufferLength < channel->bufferSize);
    descriptor_adjustStatus(
        (LegacyDescriptor*)channel->linkedChannel, STATUS_DESCRIPTOR_WRITABLE, TRUE);

    return (gssize)numCopied;
}

TransportFunctionTable channel_functions = {
    channel_close, NULL, channel_free, channel_sendUserData, channel_receiveUserData, MAGIC_VALUE};

Channel* channel_new(ChannelType type, LegacyDescriptorType dtype) {
    Channel* channel = g_new0(Channel, 1);
    MAGIC_INIT(channel);

    transport_init(&(channel->super), &channel_functions, dtype);

    channel->type = type;
    channel->buffer = bytequeue_new(8192);
    channel->bufferSize = CONFIG_PIPE_BUFFER_SIZE;

    descriptor_adjustStatus((LegacyDescriptor*)channel, STATUS_DESCRIPTOR_ACTIVE, TRUE);
    if(!(type & CT_READONLY)) {
        descriptor_adjustStatus((LegacyDescriptor*)channel, STATUS_DESCRIPTOR_WRITABLE, TRUE);
    }

    worker_count_allocation(Channel);

    return channel;
}

void channel_setLinkedChannel(Channel* channel, Channel* linkedChannel) {
    MAGIC_ASSERT(channel);

    if(channel->linkedChannel) {
        descriptor_unref(&channel->linkedChannel->super.super);
        channel->linkedChannel = NULL;
    }

    if(linkedChannel) {
      MAGIC_ASSERT(linkedChannel);
      channel->linkedChannel = linkedChannel;
      descriptor_ref(&linkedChannel->super.super);
    }
}

Channel* channel_getLinkedChannel(Channel* channel) {
    MAGIC_ASSERT(channel);
    return channel->linkedChannel;
}
