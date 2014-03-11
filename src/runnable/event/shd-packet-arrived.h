/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PACKET_ARRIVED_H_
#define SHD_PACKET_ARRIVED_H_

#include "shadow.h"

typedef struct _PacketArrivedEvent PacketArrivedEvent;

PacketArrivedEvent* packetarrived_new(Packet* packet);
void packetarrived_run(PacketArrivedEvent* event, Host* node);
void packetarrived_free(PacketArrivedEvent* event);

#endif /* SHD_PACKET_ARRIVED_H_ */
