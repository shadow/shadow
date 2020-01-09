/*
 * shd-router-queue-codel.c
 *
 *  An active queue management (AQM) algorithm implementing CoDel.
 *
 *  More info:
 *   - https://en.wikipedia.org/wiki/CoDel
 *   - http://man7.org/linux/man-pages/man8/tc-codel.8.html
 *   - https://queue.acm.org/detail.cfm?id=2209336
 *   - https://queue.acm.org/appendices/codel.html
 *
 *  Created on: Jan 9, 2020
 *      Author: rjansen
 */

#include "shadow.h"

/* hard limit of queue size, in number of packets */
#define CODEL_PARAM_LIMIT 1000
/* target minimum standing queue delay time, in milliseconds */
#define CODEL_PARAM_TARGET 5
/* delay is computed over the most recent interval time, in milliseconds */
#define CODEL_PARAM_INTERVAL 100

typedef enum _CoDelMode CoDelMode;
enum _CoDelMode {
    ROUTER_MODE_STORE, ROUTER_MODE_DROP,
};

typedef struct _RouterEntry RouterEntry;
struct _RouterEntry {
    Packet* packet;
    SimulationTime storedTS;
};

typedef struct _QueueManagerCoDel QueueManagerCoDel;
struct _QueueManagerCoDel {
    CoDelMode mode;
};

static QueueManagerCoDel* _routerqueuecodel_new() {
    QueueManagerCoDel* queueManager = g_new0(QueueManagerCoDel, 1);

    queueManager->mode = ROUTER_MODE_STORE;

    return queueManager;
}

static void _routerqueuecodel_free(QueueManagerCoDel* queueManager) {
    utility_assert(queueManager);
    g_free(queueManager);
}

static gboolean _routerqueuecodel_enqueue(QueueManagerCoDel* queueManager, Packet* packet) {
    utility_assert(queueManager);
    return FALSE;
}

static Packet* _routerqueuecodel_dequeue(QueueManagerCoDel* queueManager) {
    utility_assert(queueManager);
    return NULL;
}

static Packet* _routerqueuecodel_peek(QueueManagerCoDel* queueManager) {
    utility_assert(queueManager);
    return NULL;
}

static const struct _QueueManagerHooks _routerqueuecodel_hooks = {
    .new = (QueueManagerNew) _routerqueuecodel_new,
    .free = (QueueManagerFree) _routerqueuecodel_free,
    .enqueue = (QueueManagerEnqueue) _routerqueuecodel_enqueue,
    .dequeue = (QueueManagerDequeue) _routerqueuecodel_dequeue,
    .peek = (QueueManagerPeek) _routerqueuecodel_peek
};

const QueueManagerHooks* routerqueuecodel_getHooks() {
    return &_routerqueuecodel_hooks;
}
