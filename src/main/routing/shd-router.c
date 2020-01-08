/*
 * shd-router.c
 *
 * This component models the upstream (ISP) router from a host's external facing
 * network interface. The router uses an active queue management (AQM) algorithm
 * to smooth out packet bursts from fast networks onto slow networks. Currently,
 * the only supported AQM algorithm is CoDel.
 *
 * More info:
 *   - https://en.wikipedia.org/wiki/CoDel
 *   - http://man7.org/linux/man-pages/man8/tc-codel.8.html
 *   - https://queue.acm.org/detail.cfm?id=2209336
 *
 *  Created on: Jan 7, 2020
 *      Author: rjansen
 */

#include "shadow.h"

/* hard limit of queue size, in number of packets */
#define CODEL_PARAM_LIMIT 1000
/* target minimum standing queue delay time, in milliseconds */
#define CODEL_PARAM_TARGET 5
/* delay is computed over the most recent interval time, in milliseconds */
#define CODEL_PARAM_INTERVAL 100

typedef enum _RouterMode RouterMode;
enum _RouterMode {
    ROUTER_MODE_STORE, ROUTER_MODE_DROP,
};

typedef struct _RouterEntry RouterEntry;
struct _RouterEntry {
    Packet* packet;
    SimulationTime storedTS;
};

struct _Router {
    RouterMode currentMode;

    PacketQueuedCallback callbackFunc;
    void* callbackArg;

    Packet* currentPacket;

    gint referenceCount;
    MAGIC_DECLARE;
};

Router* router_new(PacketQueuedCallback callbackFunc, void* callbackArg) {
    Router* router = g_new0(Router, 1);
    MAGIC_INIT(router);

    router->currentMode = ROUTER_MODE_STORE;

    /* WARNING careful, we are not ref counting the callbackArg here!
     * Currently, the arg is the network interface and this is OK, but it may not be
     * OK in the general case!  */
    router->callbackFunc = callbackFunc;
    router->callbackArg = callbackArg;

    router->referenceCount = 1;

    return router;
}

static void _router_free(Router* router) {
    MAGIC_ASSERT(router);

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

void router_send(Router* router, Packet* packet) {
    MAGIC_ASSERT(router);
    /* just immediately forward the sending task to the worker, who will compute the
     * path and the appropriate delays to the destination. The packet will arrive
     * at the destination's router after a delay equal to the network latency.  */
    worker_sendPacket(packet);
}

Packet* router_receive(Router* router) {
    MAGIC_ASSERT(router);
    Packet* packet = router->currentPacket;
    router->currentPacket = NULL;
    return packet;    
}

void router_arrived(Router* router, Packet* packet) {
    MAGIC_ASSERT(router);
    utility_assert(packet);
    utility_assert(!router->currentPacket);

    router->currentPacket = packet;

    if(router->callbackFunc) {
        router->callbackFunc(router->callbackArg);
    }
}

