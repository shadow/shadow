/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _PoI {

	MAGIC_DECLARE;
};

PoI* poi_new() {
	PoI* poi = g_new0(PoI, 1);
	MAGIC_INIT(poi);

	return poi;
}

void poi_free(PoI* poi) {
	MAGIC_ASSERT(poi);

	MAGIC_CLEAR(poi);
	g_free(poi);
}
