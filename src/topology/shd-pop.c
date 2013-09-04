/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _PoP {

	MAGIC_DECLARE;
};

PoP* pop_new() {
	PoP* pop = g_new0(PoP, 1);
	MAGIC_INIT(pop);

	return pop;
}

void pop_free(PoP* pop) {
	MAGIC_ASSERT(pop);

	MAGIC_CLEAR(pop);
	g_free(pop);
}
