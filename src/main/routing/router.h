/*
 * shd-router.h
 *
 *  Created on: Jan 7, 2020
 *      Author: rjansen
 */

#ifndef SRC_MAIN_ROUTING_SHD_ROUTER_H_
#define SRC_MAIN_ROUTING_SHD_ROUTER_H_

#include <glib.h>

#include "main/routing/packet.h"

typedef struct _Router Router;
typedef enum _QueueManagerMode QueueManagerMode;
typedef struct _QueueManagerHooks QueueManagerHooks;

enum _QueueManagerMode {
    QUEUE_MANAGER_SINGLE, // buffers only a single packet
    QUEUE_MANAGER_STATIC, // a FIFO queue with a static size
    QUEUE_MANAGER_CODEL, // implements the CoDel AQM
};

typedef void* (*QueueManagerNew)();
typedef void (*QueueManagerFree)(void* queueManager);
typedef gboolean (*QueueManagerEnqueue)(void* queueManager, Packet* packet);
typedef Packet* (*QueueManagerDequeue)(void* queueManager);
typedef Packet* (*QueueManagerPeek)(void* queueManager);

struct _QueueManagerHooks {
    QueueManagerNew new;
    QueueManagerFree free;
    QueueManagerEnqueue enqueue;
    QueueManagerDequeue dequeue;
    QueueManagerPeek peek;
};

Router* router_new(QueueManagerMode queueMode, void* interface);
void router_ref(Router* router);
void router_unref(Router* router);

/* forward an outgoing packet to the destination's upstream router */
void router_forward(Router* router, Packet* packet);

/* enqueue a downstream packet, i.e., buffer it until the host can receive it */
void router_enqueue(Router* router, Packet* packet);
/* dequeue a downstream packet, i.e., receive it from the network */
Packet* router_dequeue(Router* router);

#endif /* SRC_MAIN_ROUTING_SHD_ROUTER_H_ */
