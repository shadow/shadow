/*
 * shd-router-queue-static.c
 *
 *  Created on: Jan 9, 2020
 *      Author: rjansen
 */

#include <glib.h>

#include "main/routing/packet.h"
#include "main/routing/router.h"
#include "main/utility/utility.h"

#define STATIC_PARAM_MAXSIZE 1024000

typedef struct _QueueManagerStatic QueueManagerStatic;
struct _QueueManagerStatic {
    GQueue* packets;
    guint64 totalSize;
};

static QueueManagerStatic* _routerqueuestatic_new() {
    QueueManagerStatic* queueManager = g_new0(QueueManagerStatic, 1);

    queueManager->packets = g_queue_new();

    return queueManager;
}

static void _routerqueuestatic_free(QueueManagerStatic* queueManager) {
    utility_assert(queueManager);

    if(queueManager->packets) {
        g_queue_free_full(queueManager->packets, (GDestroyNotify)packet_unref);
    }

    g_free(queueManager);
}

static inline guint64 _routerqueuestatic_getPacketLength(Packet* packet) {
    return (guint64)(packet_getPayloadLength(packet) + packet_getHeaderSize(packet));
}

static gboolean _routerqueuestatic_enqueue(QueueManagerStatic* queueManager, Packet* packet) {
    utility_assert(queueManager);
    utility_assert(packet);

    guint64 length = _routerqueuestatic_getPacketLength(packet);

    if(queueManager->totalSize + length < (guint64)STATIC_PARAM_MAXSIZE) {
        /* we will queue the packet */
        packet_ref(packet);
        g_queue_push_tail(queueManager->packets, packet);
        queueManager->totalSize += length;
        return TRUE;
    } else {
        /* not enough space, this one gets dropped */
        return FALSE;
    }
}

static Packet* _routerqueuestatic_dequeue(QueueManagerStatic* queueManager) {
    utility_assert(queueManager);

    /* this call transfers the reference that we were holding to the caller */
    Packet* packet = g_queue_pop_head(queueManager->packets);

    if(packet) {
        guint64 length = _routerqueuestatic_getPacketLength(packet);
        utility_assert(length <= queueManager->totalSize);
        queueManager->totalSize -= length;
    }

    return packet;
}

static Packet* _routerqueuestatic_peek(QueueManagerStatic* queueManager) {
    utility_assert(queueManager);
    return g_queue_peek_head(queueManager->packets);
}

static const struct _QueueManagerHooks _routerqueuestatic_hooks = {
    .new = (QueueManagerNew) _routerqueuestatic_new,
    .free = (QueueManagerFree) _routerqueuestatic_free,
    .enqueue = (QueueManagerEnqueue) _routerqueuestatic_enqueue,
    .dequeue = (QueueManagerDequeue) _routerqueuestatic_dequeue,
    .peek = (QueueManagerPeek) _routerqueuestatic_peek
};

const QueueManagerHooks* routerqueuestatic_getHooks() {
    return &_routerqueuestatic_hooks;
}
