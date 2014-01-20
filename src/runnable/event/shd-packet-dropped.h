/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PACKET_DROPPED_H_
#define SHD_PACKET_DROPPED_H_

#include "shadow.h"

typedef struct _PacketDroppedEvent PacketDroppedEvent;

PacketDroppedEvent* packetdropped_new(Packet* packet);
void packetdropped_run(PacketDroppedEvent* event, Host* node);
void packetdropped_free(PacketDroppedEvent* event);

#endif /* SHD_PACKET_DROPPED_H_ */
