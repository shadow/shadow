/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CREATE_NETWORK_H_
#define SHD_CREATE_NETWORK_H_

#include "shadow.h"

typedef struct _CreateNetworkAction CreateNetworkAction;

CreateNetworkAction* createnetwork_new(GString* name, guint64 bandwidthdown,
		guint64 bandwidthup, gdouble packetloss);
void createnetwork_run(CreateNetworkAction* action);
void createnetwork_free(CreateNetworkAction* action);

#endif /* SHD_CREATE_NETWORK_H_ */
