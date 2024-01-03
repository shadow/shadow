/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/routing/payload.h"

#include <string.h>

#include "lib/logger/logger.h"
#include "main/core/definitions.h"
#include "main/core/worker.h"
#include "main/utility/utility.h"

/* packet payloads may be shared across hosts, so we must lock access to them */
struct _Payload {
    GMutex lock;
    guint referenceCount;
    gpointer data;
    gsize length;
    MAGIC_DECLARE;
};

/* If modifying this function, you should also modify `payload_newWithMemoryManager` below. */
Payload* payload_new(const Thread* thread, UntypedForeignPtr data, gsize dataLength) {
    Payload* payload = g_new0(Payload, 1);
    MAGIC_INIT(payload);

    if (data.val && dataLength > 0) {
        payload->data = g_malloc0(dataLength);
        if (process_readPtr(thread_getProcess(thread), payload->data, data, dataLength) != 0) {
            warning("Couldn't read data for packet");
            g_free(payload);
            return NULL;
        }
        utility_debugAssert(payload->data != NULL);
        payload->length = dataLength;
    }

    g_mutex_init(&(payload->lock));
    payload->referenceCount = 1;

    worker_count_allocation(Payload);

    return payload;
}

/* This is a copy of `payload_new` but passes the memory manager through. Once we've moved
 * UDP sockets to rust, we can remove `payload_new` and rename this function to
 * `payload_new`. */
Payload* payload_newWithMemoryManager(UntypedForeignPtr data, gsize dataLength,
                                      const MemoryManager* mem) {
    Payload* payload = g_new0(Payload, 1);
    MAGIC_INIT(payload);

    if (data.val && dataLength > 0) {
        payload->data = g_malloc0(dataLength);
        if (memorymanager_readPtr(mem, payload->data, data, dataLength) != 0) {
            warning("Couldn't read data for packet");
            g_free(payload);
            return NULL;
        }
        utility_debugAssert(payload->data != NULL);
        payload->length = dataLength;
    }

    g_mutex_init(&(payload->lock));
    payload->referenceCount = 1;

    worker_count_allocation(Payload);

    return payload;
}

Payload* payload_newFromShadow(const void* data, gsize dataLength) {
    Payload* payload = g_new0(Payload, 1);
    MAGIC_INIT(payload);

    if (data && dataLength > 0) {
        payload->data = g_malloc0(dataLength);
        utility_debugAssert(payload->data != NULL);
        memcpy(payload->data, data, dataLength);
        payload->length = dataLength;
    }

    g_mutex_init(&(payload->lock));
    payload->referenceCount = 1;

    worker_count_allocation(Payload);

    return payload;
}

static void _payload_free(Payload* payload) {
    MAGIC_ASSERT(payload);

    g_mutex_clear(&(payload->lock));

    if(payload->data) {
        g_free(payload->data);
    }

    MAGIC_CLEAR(payload);
    g_free(payload);

    worker_count_deallocation(Payload);
}

static void _payload_lock(Payload* payload) {
    MAGIC_ASSERT(payload);
    g_mutex_lock(&(payload->lock));
}

static void _payload_unlock(Payload* payload) {
    MAGIC_ASSERT(payload);
    g_mutex_unlock(&(payload->lock));
}

void payload_ref(Payload* payload) {
    MAGIC_ASSERT(payload);
    _payload_lock(payload);
    (payload->referenceCount)++;
    _payload_unlock(payload);
}

void payload_unref(Payload* payload) {
    MAGIC_ASSERT(payload);
    _payload_lock(payload);
    (payload->referenceCount)--;
    gboolean needsFree = (payload->referenceCount == 0) ? TRUE : FALSE;
    _payload_unlock(payload);
    if(needsFree) {
        _payload_free(payload);
    }
}

gsize payload_getLength(Payload* payload) {
    MAGIC_ASSERT(payload);
    _payload_lock(payload);
    gsize length = payload->length;
    _payload_unlock(payload);
    return length;
}

/* If modifying this function, you should also modify `payload_getDataWithMemoryManager` below. */
gssize payload_getData(Payload* payload, const Thread* thread, gsize offset,
                       UntypedForeignPtr destBuffer, gsize destBufferLength) {
    MAGIC_ASSERT(payload);

    _payload_lock(payload);

    utility_debugAssert(offset <= payload->length);

    gssize targetLength = payload->length - offset;
    gssize copyLength = MIN(targetLength, destBufferLength);

    if (copyLength > 0) {
        int err = process_writePtr(
            thread_getProcess(thread), destBuffer, payload->data + offset, copyLength);
        if (err) {
            _payload_unlock(payload);
            return err;
        }
    }

    _payload_unlock(payload);

    return copyLength;
}

/* This is a copy of `payload_getData` but passes the memory manager through. Once we've moved
 * UDP sockets to rust, we can remove `payload_getData` and rename this function to
 * `payload_getData`. */
gssize payload_getDataWithMemoryManager(Payload* payload, gsize offset,
                                        UntypedForeignPtr destBuffer, gsize destBufferLength,
                                        MemoryManager* mem) {
    MAGIC_ASSERT(payload);

    _payload_lock(payload);

    utility_debugAssert(offset <= payload->length);

    gssize targetLength = payload->length - offset;
    gssize copyLength = MIN(targetLength, destBufferLength);

    if (copyLength > 0) {
        int err =
            memorymanager_writePtr(mem, destBuffer, payload->data + offset, copyLength);
        if (err) {
            _payload_unlock(payload);
            return err;
        }
    }

    _payload_unlock(payload);

    return copyLength;
}

gsize payload_getDataShadow(Payload* payload, gsize offset, void* destBuffer,
                            gsize destBufferLength) {
    MAGIC_ASSERT(payload);

    _payload_lock(payload);

    utility_debugAssert(offset <= payload->length);

    gsize targetLength = payload->length - offset;
    gsize copyLength = MIN(targetLength, destBufferLength);

    if(copyLength > 0) {
        memcpy(destBuffer, payload->data + offset, copyLength);
    }

    _payload_unlock(payload);

    return copyLength;
}
