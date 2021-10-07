/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_CONTROLLER_H_
#define SHD_CONTROLLER_H_

#include <glib.h>

typedef struct _Controller Controller;

#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"

Controller* controller_new(const ConfigOptions*);
void controller_free(Controller*);
gint controller_run(Controller*);

gdouble controller_getRunTimeElapsed(Controller*);

gboolean controller_managerFinishedCurrentRound(Controller*, SimulationTime, SimulationTime*,
                                                SimulationTime*);
void controller_updateMinRunahead(Controller* controller, SimulationTime minPathLatency);
SimulationTime controller_getLatency(Controller* controller, Address* srcAddress,
                                     Address* dstAddress);
gfloat controller_getReliability(Controller* controller, Address* srcAddress, Address* dstAddress);
bool controller_isRoutable(Controller* controller, Address* srcAddress, Address* dstAddress);
void controller_incrementPacketCount(Controller* controller, Address* srcAddress,
                                     Address* dstAddress);

// TODO remove these eventually since they cant be shared accross remote managers
DNS* controller_getDNS(Controller* controller);

#endif /* SHD_CONTROLLER_H_ */
