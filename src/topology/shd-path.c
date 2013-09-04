/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Path {

	MAGIC_DECLARE;
};

Path* path_new() {
	Path* path = g_new0(Path, 1);
	MAGIC_INIT(path);

	return path;
}

void path_free(Path* path) {
	MAGIC_ASSERT(path);

	MAGIC_CLEAR(path);
	g_free(path);
}
