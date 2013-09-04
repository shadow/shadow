/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Topology {

	MAGIC_DECLARE;
};

Topology* topology_new() {
	Topology* topology = g_new0(Topology, 1);
	MAGIC_INIT(topology);

	return topology;
}

void topology_free(Topology* topology) {
	MAGIC_ASSERT(topology);

	MAGIC_CLEAR(topology);
	g_free(topology);
}
