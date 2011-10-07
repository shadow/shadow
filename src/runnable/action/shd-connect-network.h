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

#ifndef SHD_CONNECT_NETWORK_H_
#define SHD_CONNECT_NETWORK_H_

#include "shadow.h"

typedef struct _ConnectNetworkAction ConnectNetworkAction;

struct _ConnectNetworkAction {
	Action super;
	GString* networkaName;
	GString* networkbName;
	GString* latencyabCDFName;
	gdouble reliabilityab;
	GString* latencybaCDFName;
	gdouble reliabilityba;
	MAGIC_DECLARE;
};

ConnectNetworkAction* connectnetwork_new(GString* networkaName,
		GString* networkbName, GString* latencyabCDFName,
		gdouble reliabilityab, GString* latencybaCDFName,
		gdouble reliabilityba);
void connectnetwork_run(ConnectNetworkAction* action);
void connectnetwork_free(ConnectNetworkAction* action);

#endif /* SHD_CONNECT_NETWORK_H_ */
