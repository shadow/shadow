/*
 * shd-router.c
 *
 * This component models the upstream (ISP) router from a host's external facing
 * network interface. The router uses a queue management algorithm to smooth out
 * packet bursts from fast networks onto slow networks.
 *
 *  Created on: Jan 7, 2020
 *      Author: rjansen
 */

#include "shadow.h"


struct _Router {
    /* the algorithm we use to manage the router queue */
    QueueManagerMode queueMode;
    const QueueManagerHooks* queueHooks;
    void* queueManager;

    /* the interface that we deliver packets to */
    NetworkInterface* interface;

    gint referenceCount;
    MAGIC_DECLARE;
};

Router* router_new(QueueManagerMode queueMode, void* interface) {
    utility_assert(interface);

    Router* router = g_new0(Router, 1);
    MAGIC_INIT(router);

    router->queueMode = queueMode;
    router->interface = interface;

    router->referenceCount = 1;

    if(router->queueMode == QUEUE_MANAGER_SINGLE) {
        router->queueHooks = routerqueuesingle_getHooks();
    } else if(router->queueMode == QUEUE_MANAGER_STATIC) {
        router->queueHooks = routerqueuestatic_getHooks();
    } else if(router->queueMode == QUEUE_MANAGER_CODEL) {
        router->queueHooks = routerqueuecodel_getHooks();
    } else {
        error("Queue manager mode %i is undefined", (int)queueMode);
    }

    utility_assert(router->queueHooks->new);
    utility_assert(router->queueHooks->free);
    utility_assert(router->queueHooks->enqueue);
    utility_assert(router->queueHooks->dequeue);
    utility_assert(router->queueHooks->peek);

    router->queueManager = router->queueHooks->new();

    return router;
}

static void _router_free(Router* router) {
    MAGIC_ASSERT(router);

    router->queueHooks->free(router->queueManager);

    MAGIC_CLEAR(router);
    g_free(router);
}

void router_ref(Router* router) {
    MAGIC_ASSERT(router);
    (router->referenceCount)++;
}

void router_unref(Router* router) {
    MAGIC_ASSERT(router);
    (router->referenceCount)--;
    utility_assert(router->referenceCount >= 0);
    if(router->referenceCount == 0) {
        _router_free(router);
    }
}

void router_forward(Router* router, Packet* packet) {
    MAGIC_ASSERT(router);
    /* just immediately forward the sending task to the worker, who will compute the
     * path and the appropriate delays to the destination. The packet will arrive
     * at the destination's router after a delay equal to the network latency.  */
    worker_sendPacket(packet);
}

void router_enqueue(Router* router, Packet* packet) {
    MAGIC_ASSERT(router);
    utility_assert(packet);

    Packet* bufferedPacket = router->queueHooks->peek(router->queueManager);

    gboolean wasQueued = router->queueHooks->enqueue(router->queueManager, packet);

    if(wasQueued) {
        packet_addDeliveryStatus(packet, PDS_ROUTER_ENQUEUED);
    } else {
        packet_addDeliveryStatus(packet, PDS_ROUTER_DROPPED);
    }

    /* notify the netiface that we have a new packet so it can dequeue it. */
    if(!bufferedPacket && wasQueued) {
        networkinterface_receivePackets(router->interface);
    }
}

Packet* router_dequeue(Router* router) {
    MAGIC_ASSERT(router);

    Packet* packet = router->queueHooks->dequeue(router->queueManager);
    if(packet) {
        packet_addDeliveryStatus(packet, PDS_ROUTER_DEQUEUED);
    }

    return packet;
}
