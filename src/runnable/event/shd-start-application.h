/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#ifndef SHD_START_APPLICATION_H_
#define SHD_START_APPLICATION_H_

#include "shadow.h"

/**
 * Start an application for a given Node. This event is basically a bootstrap
 * event for each node and is therefore unique in that it requires a reference
 * to the node and the execution time. (Generally the node reference and time
 * are set automatically.)
 */

typedef struct _StartApplicationEvent StartApplicationEvent;

struct _StartApplicationEvent {
	Event super;
	guint spin_seconds;

	MAGIC_DECLARE;
};

StartApplicationEvent* startapplication_new(Node* node, SimulationTime time);
void startapplication_run(StartApplicationEvent* event, Node* node);
void startapplication_free(StartApplicationEvent* event);

#endif /* SHD_START_APPLICATION_H_ */
