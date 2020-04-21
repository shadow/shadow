/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_ENGINE_H_
#define SHD_ENGINE_H_

#include <glib.h>

#include "core/support/definitions.h"
#include "core/support/options.h"
#include "routing/address.h"
#include "routing/dns.h"
#include "routing/topology.h"

typedef struct _Master Master;

Master* master_new(Options*);
void master_free(Master*);
gint master_run(Master*);

void master_updateMinTimeJump(Master*, gdouble);
gdouble master_getRunTimeElapsed(Master*);

gboolean master_slaveFinishedCurrentRound(Master*, SimulationTime, SimulationTime*, SimulationTime*);
gdouble master_getLatency(Master* master, Address* srcAddress, Address* dstAddress);

// TODO remove these eventually since they cant be shared accross remote slaves
DNS* master_getDNS(Master* master);
Topology* master_getTopology(Master* master);

#endif /* SHD_ENGINE_H_ */
