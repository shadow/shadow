/*
 * shd-router.h
 *
 *  Created on: Jan 7, 2020
 *      Author: rjansen
 */

#ifndef SRC_MAIN_ROUTING_SHD_ROUTER_H_
#define SRC_MAIN_ROUTING_SHD_ROUTER_H_

typedef void (*PacketQueuedCallback)(void* callbackArg);

typedef struct _Router Router;

#include "shadow.h"

Router* router_new(PacketQueuedCallback callbackFunc, void* callbackArg);
void router_ref(Router* router);
void router_unref(Router* router);

void router_send(Router* router, Packet* packet);
void router_arrived(Router* router, Packet* packet);
Packet* router_receive(Router* router);

#endif /* SRC_MAIN_ROUTING_SHD_ROUTER_H_ */
