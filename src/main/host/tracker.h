/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TRACKER_H_
#define SHD_TRACKER_H_

#include <glib.h>
#include <netinet/in.h>

#include "lib/logger/log_level.h"
#include "main/core/support/definitions.h"
#include "main/host/protocol.h"
#include "main/host/tracker_types.h"
#include "main/routing/packet.minimal.h"

Tracker* tracker_new(const Host* host, CSimulationTime interval, LogLevel loglevel,
                     LogInfoFlags loginfo);
void tracker_free(Tracker* tracker);

void tracker_addProcessingTime(Tracker* tracker, CSimulationTime processingTime);
void tracker_addVirtualProcessingDelay(Tracker* tracker, CSimulationTime delay);
void tracker_addInputBytes(Tracker* tracker, Packet* packet, LegacySocket* socket);
void tracker_addOutputBytes(Tracker* tracker, Packet* packet, LegacySocket* socket);
void tracker_addAllocatedBytes(Tracker* tracker, gpointer location, gsize allocatedBytes);
void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location);
void tracker_addSocket(Tracker* tracker, LegacySocket* socket, ProtocolType type, gsize inputBufferSize, gsize outputBufferSize);
void tracker_updateSocketPeer(Tracker* tracker, LegacySocket* socket, in_addr_t peerIP, in_port_t peerPort);
void tracker_updateSocketInputBuffer(Tracker* tracker, LegacySocket* socket, gsize inputBufferLength, gsize inputBufferSize);
void tracker_updateSocketOutputBuffer(Tracker* tracker, LegacySocket* socket, gsize outputBufferLength, gsize outputBufferSize);
void tracker_removeSocket(Tracker* tracker, LegacySocket* socket);
void tracker_heartbeat(Tracker* tracker, const Host* host);
static inline void tracker_heartbeatTask(const Host* host, gpointer tracker, gpointer userData) {
    tracker_heartbeat(tracker, host);
}

#endif /* SHD_TRACKER_H_ */
