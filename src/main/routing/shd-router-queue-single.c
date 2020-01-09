/*
 * shd-router-queue-single.c
 *
 *  Created on: Jan 9, 2020
 *      Author: rjansen
 */

#include "shadow.h"

typedef struct _QueueManagerSingle QueueManagerSingle;
struct _QueueManagerSingle {
    Packet* currentPacket;
};

static QueueManagerSingle* _routerqueuesingle_new() {
    QueueManagerSingle* queueManager = g_new0(QueueManagerSingle, 1);
    return queueManager;
}

static void _routerqueuesingle_free(QueueManagerSingle* queueManager) {
    utility_assert(queueManager);
    if(queueManager->currentPacket) {
        packet_unref(queueManager->currentPacket);
    }
    g_free(queueManager);
}

static gboolean _routerqueuesingle_enqueue(QueueManagerSingle* queueManager, Packet* packet) {
    utility_assert(queueManager);

    if(!queueManager->currentPacket) {
        /* we will queue the packet */
        packet_ref(packet);
        queueManager->currentPacket = packet;
        return TRUE;
    } else {
        /* we already queued a packet, so this one gets dropped */
        return FALSE;
    }
}

static Packet* _routerqueuesingle_dequeue(QueueManagerSingle* queueManager) {
    utility_assert(queueManager);
    /* this call transfers the reference that we were holding to the caller */
    Packet* packet = queueManager->currentPacket;
    queueManager->currentPacket = NULL;
    return packet;
}

static Packet* _routerqueuesingle_peek(QueueManagerSingle* queueManager) {
    utility_assert(queueManager);
    return queueManager->currentPacket;
}

static const struct _QueueManagerHooks _routerqueuesingle_hooks = {
    .new = (QueueManagerNew) _routerqueuesingle_new,
    .free = (QueueManagerFree) _routerqueuesingle_free,
    .enqueue = (QueueManagerEnqueue) _routerqueuesingle_enqueue,
    .dequeue = (QueueManagerDequeue) _routerqueuesingle_dequeue,
    .peek = (QueueManagerPeek) _routerqueuesingle_peek
};

const QueueManagerHooks* routerqueuesingle_getHooks() {
    return &_routerqueuesingle_hooks;
}
