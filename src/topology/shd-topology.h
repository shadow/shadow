/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_TOPOLOGY_H_
#define SHD_TOPOLOGY_H_

#include "shadow.h"

typedef struct _Topology Topology;

Topology* topology_new();
void topology_free(Topology* topology);

#endif /* SHD_TOPOLOGY_H_ */
