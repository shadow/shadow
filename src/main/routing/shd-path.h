/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PATH_H_
#define SHD_PATH_H_

#include "shadow.h"

typedef struct _Path Path;

Path* path_new(gint64 srcVertexIndex, gint64 dstVertexIndex, gdouble latency, gdouble reliability);
void path_free(Path* path);
gdouble path_getLatency(Path* path);
gdouble path_getReliability(Path* path);
void path_incrementPacketCount(Path* path);
void path_toString(Path* path, GString* string);

#endif /* SHD_PATH_H_ */
