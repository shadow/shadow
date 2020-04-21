/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/routing/payload.h"

#include <string.h>

#include "main/core/worker.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/utility/utility.h"

/* packet payloads may be shared across hosts, so we must lock access to them */
struct _Payload {
    GMutex lock;
    guint referenceCount;
    gpointer data;
    gsize length;
    MAGIC_DECLARE;
};

Payload* payload_new(gconstpointer data, gsize dataLength) {
    Payload* payload = g_new0(Payload, 1);
    MAGIC_INIT(payload);

    g_mutex_init(&(payload->lock));
    payload->referenceCount = 1;

    if(data && dataLength > 0) {
        payload->data = g_malloc0(dataLength);
        memmove(payload->data, data, dataLength);
        utility_assert(payload->data != NULL);
        payload->length = dataLength;
    }

    worker_countObject(OBJECT_TYPE_PAYLOAD, COUNTER_TYPE_NEW);

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

    worker_countObject(OBJECT_TYPE_PAYLOAD, COUNTER_TYPE_FREE);
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

gsize payload_getData(Payload* payload, gsize offset, gpointer destBuffer, gsize destBufferLength) {
    MAGIC_ASSERT(payload);

    _payload_lock(payload);

    utility_assert(offset <= payload->length);

    gsize targetLength = payload->length - offset;
    gsize copyLength = MIN(targetLength, destBufferLength);

    if(copyLength > 0) {
        g_memmove(destBuffer, payload->data + offset, copyLength);
    }

    _payload_unlock(payload);

    return copyLength;
}
