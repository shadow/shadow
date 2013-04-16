/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#ifndef SHD_TRACKER_H_
#define SHD_TRACKER_H_

typedef struct _Tracker Tracker;

Tracker* tracker_new();
void tracker_free(Tracker* tracker);

void tracker_addProcessingTime(Tracker* tracker, SimulationTime processingTime);
void tracker_addVirtualProcessingDelay(Tracker* tracker, SimulationTime delay);
void tracker_addInputBytes(Tracker* tracker, gsize inputBytes);
void tracker_addOutputBytes(Tracker* tracker, gsize outputBytes);
void tracker_addAllocatedBytes(Tracker* tracker, gpointer location, gsize allocatedBytes);
void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location);
void tracker_heartbeat(Tracker* tracker);

#endif /* SHD_TRACKER_H_ */
