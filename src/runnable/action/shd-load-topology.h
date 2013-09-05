/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_LOAD_TOPOLOGY_H_
#define SHD_LOAD_TOPOLOGY_H_

#include "shadow.h"

typedef struct _LoadTopologyAction LoadTopologyAction;

LoadTopologyAction* loadtopology_new(GString* path);
void loadtopology_run(LoadTopologyAction* action);
void loadtopology_free(LoadTopologyAction* action);

#endif /* SHD_LOAD_TOPOLOGY_H_ */
