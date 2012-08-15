/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SHD_TRACKER_H_
#define SHD_TRACKER_H_

typedef struct _Tracker Tracker;

Tracker* tracker_new();
void tracker_free(Tracker* tracker);

void tracker_addProcessingTime(Tracker* tracker, SimulationTime processingTime);
void tracker_addInputBytes(Tracker* tracker, gsize inputBytes);
void tracker_addOutputBytes(Tracker* tracker, gsize outputBytes);
void tracker_addAllocatedBytes(Tracker* tracker, gpointer location, gsize allocatedBytes);
void tracker_removeAllocatedBytes(Tracker* tracker, gpointer location);
void tracker_heartbeat(Tracker* tracker);

#endif /* SHD_TRACKER_H_ */
