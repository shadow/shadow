/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_PATH_H_
#define SHD_PATH_H_

#include <glib.h>

typedef struct _Path Path;

Path* path_new(gboolean isDirect, gint64 srcVertexIndex, gint64 dstVertexIndex, gdouble latency, gdouble reliability);
void path_free(Path* path);

gdouble path_getLatency(Path* path);
gdouble path_getReliability(Path* path);

void path_incrementPacketCount(Path* path);

gchar* path_toString(Path* path);

gint64 path_getSrcVertexIndex(Path* path);
gint64 path_getDstVertexIndex(Path* path);

#endif /* SHD_PATH_H_ */
