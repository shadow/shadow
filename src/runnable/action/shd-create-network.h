/*
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

#ifndef SHD_CREATE_NETWORK_H_
#define SHD_CREATE_NETWORK_H_

#include "shadow.h"

typedef struct _CreateNetworkAction CreateNetworkAction;

struct _CreateNetworkAction {
	Action super;
	GQuark id;
	GQuark latencyID;
	gdouble reliability;
	MAGIC_DECLARE;
};

CreateNetworkAction* createnetwork_new(GString* name, GString* latencyCDFName,
		gdouble reliability);
void createnetwork_run(CreateNetworkAction* action);
void createnetwork_free(CreateNetworkAction* action);

#endif /* SHD_CREATE_NETWORK_H_ */
