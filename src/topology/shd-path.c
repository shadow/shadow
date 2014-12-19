/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Path {
    gdouble latency;
    gdouble reliablity;
    MAGIC_DECLARE;
};

Path* path_new(gdouble latency, gdouble reliablity) {
    Path* path = g_new0(Path, 1);
    MAGIC_INIT(path);

    path->latency = latency;
    path->reliablity = reliablity;

    return path;
}

void path_free(Path* path) {
    MAGIC_ASSERT(path);

    MAGIC_CLEAR(path);
    g_free(path);
}

gdouble path_getLatency(Path* path) {
    MAGIC_ASSERT(path);
    return path->latency;
}

gdouble path_getReliability(Path* path) {
    MAGIC_ASSERT(path);
    return path->reliablity;
}
