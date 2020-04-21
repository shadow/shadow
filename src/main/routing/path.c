/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "routing/path.h"

#include <stddef.h>

#include "utility/utility.h"

struct _Path {
    gboolean isDirect;
    gint64 srcVertexIndex;
    gint64 dstVertexIndex;
    gdouble latency;
    gdouble reliability;
    guint64 packetCount;
    MAGIC_DECLARE;
};

Path* path_new(gboolean isDirect, gint64 srcVertexIndex, gint64 dstVertexIndex, gdouble latency, gdouble reliability) {
    Path* path = g_new0(Path, 1);
    MAGIC_INIT(path);

    /* a path representing a single edge in the graph.
     *   SrcVertex--Edge--DstVertex: isDirect should be TRUE
     *   SrcVertex--Edge--Vertex--Edge--DstVertex: isDirect should be FALSE
     */
    path->isDirect = isDirect;
    path->srcVertexIndex = srcVertexIndex;
    path->dstVertexIndex = dstVertexIndex;
    path->latency = latency;
    path->reliability = reliability;

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
    return path->reliability;
}

void path_incrementPacketCount(Path* path) {
    MAGIC_ASSERT(path);
    path->packetCount++;
}

gchar* path_toString(Path* path) {
    MAGIC_ASSERT(path);

    GString* pathStringBuffer = g_string_new(NULL);

    g_string_printf(pathStringBuffer,
            "SourceIndex=%"G_GINT64_FORMAT" DestinationIndex=%"G_GINT64_FORMAT" "
            "Latency=%f Reliability=%f PacketCount=%"G_GUINT64_FORMAT" isDirect=%s",
            path->srcVertexIndex, path->dstVertexIndex,
            path->latency, path->reliability, path->packetCount,
            path->isDirect ? "True" : "False");

    return g_string_free(pathStringBuffer, FALSE);
}

gint64 path_getSrcVertexIndex(Path* path) {
    MAGIC_ASSERT(path);
    return path->srcVertexIndex;
}

gint64 path_getDstVertexIndex(Path* path) {
    MAGIC_ASSERT(path);
    return path->dstVertexIndex;
}
