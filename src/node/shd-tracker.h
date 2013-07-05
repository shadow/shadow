/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TRACKER_H_
#define SHD_TRACKER_H_

typedef struct _Tracker Tracker;

Tracker* tracker_new(SimulationTime interval, GLogLevelFlags loglevel, gchar* flagString);
void tracker_free(Tracker* tracker);

void tracker_addProcessingTime(Tracker* tracker, SimulationTime processingTime);
void tracker_addVirtualProcessingDelay(Tracker* tracker, SimulationTime delay);
void tracker_addInputBytes(Tracker* tracker, gsize inputBytes);
void tracker_addOutputBytes(Tracker* tracker, gsize outputBytes);
void tracker_addAllocatedBytes(Tracker* tracker, gpointer location, gsize allocatedBytes);
void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location);
void tracker_addSocket(Tracker* tracker, gint handle, gsize inputBufferSize, gsize outputBufferSize);
void tracker_updateSocketPeer(Tracker* tracker, gint handle, in_addr_t peerIP, in_port_t peerPort);
void tracker_updateSocketInputBuffer(Tracker* tracker, gint handle, gsize inputBufferLength, gsize inputBufferSize);
void tracker_updateSocketOutputBuffer(Tracker* tracker, gint handle, gsize outputBufferLength, gsize outputBufferSize);
void tracker_removeSocket(Tracker* tracker, gint handle);
void tracker_heartbeat(Tracker* tracker);

#endif /* SHD_TRACKER_H_ */
