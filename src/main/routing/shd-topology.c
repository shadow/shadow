/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Topology {
    /* the imported igraph graph data - operations on it after initializations
     * MUST be locked in cases where igraph is not thread-safe! */
    igraph_t graph;
    GMutex graphLock;

    /* the edge weights currently used when computing shortest paths.
     * this is protected by its own lock */
    igraph_vector_t* edgeWeights;
    GRWLock edgeWeightsLock;

    /* each connected virtual host is assigned to a PoI vertex. we store the mapping to the
     * vertex index so we can correctly lookup the assigned edge when computing latency.
     * virtualIP->vertexIndex (stored as pointer) */
    GHashTable* virtualIP;
    GRWLock virtualIPLock;

    /* cached latencies to avoid excessive shortest path lookups
     * store a cache table for every connected address
     * fromAddress->toAddress->Path* */
    GHashTable* pathCache;
    gdouble minimumPathLatency;
    GRWLock pathCacheLock;

    /******/
    /* START - items protected by a global topology lock */
    GMutex topologyLock;

    /* graph properties of the imported graph */
    igraph_integer_t clusterCount;
    igraph_integer_t vertexCount;
    igraph_integer_t edgeCount;
    igraph_bool_t isConnected;
    igraph_bool_t isDirected;
    igraph_bool_t isComplete;

    /* keep track of how many, and how long we spend computing shortest paths */
    gdouble shortestPathTotalTime;
    guint shortestPathCount;

    /* END global topology lock */
    /******/

    MAGIC_DECLARE;
};

typedef struct _AttachHelper AttachHelper;
struct _AttachHelper {
    GQueue* candidatesAll;
    guint numCandidatesAllIPs;
    GQueue* candidatesType;
    guint numCandidatesTypeIPs;
    GQueue* candidatesCode;
    guint numCandidatesCodeIPs;
    GQueue* candidatesTypeCode;
    guint numCandidatesTypeCodeIPs;
    gchar* typeHint;
    gchar* geocodeHint;
    gchar* ipHint;
    in_addr_t requestedIP;
    gboolean foundExactIPMatch;
};

typedef void (*EdgeNotifyFunc)(Topology* top, igraph_integer_t edgeIndex, gpointer userData);
typedef void (*VertexNotifyFunc)(Topology* top, igraph_integer_t vertexIndex, gpointer userData);

#if 1//!defined(IGRAPH_THREAD_SAFE) || (defined(IGRAPH_THREAD_SAFE) && IGRAPH_THREAD_SAFE == 0)
static void _topology_initGraphLock(GMutex* graphLockPtr) {
    g_mutex_init(graphLockPtr);
}
static void _topology_clearGraphLock(GMutex* graphLockPtr) {
    g_mutex_clear(graphLockPtr);
}
static void _topology_lockGraph(Topology* top) {
    g_mutex_lock(&top->graphLock);
}
static void _topology_unlockGraph(Topology* top) {
    g_mutex_unlock(&top->graphLock);
}
#else
#define _topology_initGraphLock(graphLockPtr)
#define _topology_clearGraphLock(graphLockPtr)
#define _topology_lockGraph(top)
#define _topology_unlockGraph(top)
#endif

static gboolean _topology_loadGraph(Topology* top, const gchar* graphPath) {
    MAGIC_ASSERT(top);
    /* initialize the built-in C attribute handler */
    igraph_attribute_table_t* oldHandler = igraph_i_set_attribute_table(&igraph_cattribute_table);

    /* get the file */
    FILE* graphFile = fopen(graphPath, "r");
    if(!graphFile) {
        critical("fopen returned NULL while attempting to open graph file path '%s', error %i: %s",
                graphPath, errno, strerror(errno));
        return FALSE;
    }

    _topology_lockGraph(top);
    message("reading graphml topology graph at '%s'...", graphPath);
    gint result = igraph_read_graph_graphml(&top->graph, graphFile, 0);
    _topology_unlockGraph(top);

    fclose(graphFile);

    if(result != IGRAPH_SUCCESS) {
        critical("igraph_read_graph_graphml return non-success code %i", result);
        return FALSE;
    }

    message("successfully read graphml topology graph at '%s'", graphPath);

    return TRUE;
}

static gboolean _topology_checkGraphProperties(Topology* top) {
    MAGIC_ASSERT(top);
    gint result = 0;

    message("checking graph properties...");

    /* IGRAPH_WEAK means the undirected version of the graph is connected
     * IGRAPH_STRONG means a vertex can reach all others via a directed path
     * we must be able to send packets in both directions, so we want IGRAPH_STRONG */
    result = igraph_is_connected(&top->graph, &(top->isConnected), IGRAPH_STRONG);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_is_connected return non-success code %i", result);
        return FALSE;
    }

    igraph_integer_t clusterCount;
    result = igraph_clusters(&top->graph, NULL, NULL, &(top->clusterCount), IGRAPH_STRONG);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_clusters return non-success code %i", result);
        return FALSE;
    }

    /* it must be connected */
    if(!top->isConnected || top->clusterCount > 1) {
        critical("topology must be but is not strongly connected");
        return FALSE;
    }

    top->isDirected = igraph_is_directed(&top->graph);

    /* the topology is complete if the largest clique includes all vertices */
    igraph_integer_t largestCliqueSize = 0;
    result = igraph_clique_number(&top->graph, &largestCliqueSize);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_clique_number return non-success code %i", result);
        return FALSE;
    }

    if(largestCliqueSize == igraph_vcount(&top->graph)) {
        top->isComplete = (igraph_bool_t)TRUE;
    }

    message("topology graph is %s, %s, and %s with %u %s",
            top->isComplete ? "complete" : "incomplete",
            top->isDirected ? "directed" : "undirected",
            top->isConnected ? "strongly connected" : "disconnected",
            (guint)top->clusterCount, top->clusterCount == 1 ? "cluster" : "clusters");

    message("checking graph attributes...");

    /* now check list of all attributes */
    igraph_strvector_t gnames, vnames, enames;
    igraph_vector_t gtypes, vtypes, etypes;
    igraph_strvector_init(&gnames, 25);
    igraph_vector_init(&gtypes, 25);
    igraph_strvector_init(&vnames, 25);
    igraph_vector_init(&vtypes, 25);
    igraph_strvector_init(&enames, 25);
    igraph_vector_init(&etypes, 25);

    result = igraph_cattribute_list(&top->graph, &gnames, &gtypes, &vnames, &vtypes, &enames, &etypes);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_cattribute_list return non-success code %i", result);
        return FALSE;
    }

    gint i = 0;
    for(i = 0; i < igraph_strvector_size(&gnames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&gnames, (glong) i, &name);
        debug("found graph attribute '%s'", name);
    }
    for(i = 0; i < igraph_strvector_size(&vnames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&vnames, (glong) i, &name);
        debug("found vertex attribute '%s'", name);
    }
    for(i = 0; i < igraph_strvector_size(&enames); i++) {
        gchar* name = NULL;
        igraph_strvector_get(&enames, (glong) i, &name);
        debug("found edge attribute '%s'", name);
    }

    igraph_strvector_destroy(&gnames);
    igraph_vector_destroy(&gtypes);
    igraph_strvector_destroy(&vnames);
    igraph_vector_destroy(&vtypes);
    igraph_strvector_destroy(&enames);
    igraph_vector_destroy(&etypes);

    message("successfully verified graph attributes");

    return TRUE;
}

static void _topology_checkGraphVerticesHelperHook(Topology* top, igraph_integer_t vertexIndex, gpointer userData) {
    MAGIC_ASSERT(top);

    /* get vertex attributes: S for string and N for numeric */
    const gchar* idStr = VAS(&top->graph, "id", vertexIndex);
    const gchar* typeStr = VAS(&top->graph, "type", vertexIndex);

    if(g_strstr_len(idStr, (gssize)-1, "poi")) {
        const gchar* ipStr = VAS(&top->graph, "ip", vertexIndex);
        const gchar* geocodeStr = VAS(&top->graph, "geocode", vertexIndex);
        igraph_real_t bwup = VAN(&top->graph, "bandwidthup", vertexIndex);
        igraph_real_t bwdown = VAN(&top->graph, "bandwidthdown", vertexIndex);
        igraph_real_t ploss = VAN(&top->graph, "packetloss", vertexIndex);

        debug("found vertex %li (%s), type=%s ip=%s geocode=%s "
                "bandwidthup=%f bandwidthdown=%f packetloss=%f",
                (glong)vertexIndex, idStr, typeStr, ipStr, geocodeStr, bwup, bwdown, ploss);
    } else {
        debug("found vertex %li (%s), type=%s",
                (glong)vertexIndex, idStr, typeStr);
    }
}

static igraph_integer_t _topology_iterateAllVertices(Topology* top, VertexNotifyFunc hook, gpointer userData) {
    MAGIC_ASSERT(top);
    utility_assert(hook);

    /* we will iterate through the vertices */
    igraph_vit_t vertexIterator;
    gint result = igraph_vit_create(&top->graph, igraph_vss_all(), &vertexIterator);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_vit_create return non-success code %i", result);
        return -1;
    }

    /* count the vertices as we iterate */
    igraph_integer_t vertexCount = 0;
    while (!IGRAPH_VIT_END(vertexIterator)) {
        long int vertexIndex = IGRAPH_VIT_GET(vertexIterator);

        /* call the hook function for each edge */
        hook(top, (igraph_integer_t) vertexIndex, userData);

        vertexCount++;
        IGRAPH_VIT_NEXT(vertexIterator);
    }

    /* clean up */
    igraph_vit_destroy(&vertexIterator);

    return vertexCount;
}

static gboolean _topology_checkGraphVertices(Topology* top) {
    MAGIC_ASSERT(top);

    message("checking graph vertices...");

    igraph_integer_t vertexCount = _topology_iterateAllVertices(top, _topology_checkGraphVerticesHelperHook, NULL);
    if(vertexCount < 0) {
        /* there was some kind of error */
        return FALSE;
    }

    top->vertexCount = igraph_vcount(&top->graph);
    if(top->vertexCount != vertexCount) {
        warning("igraph_vcount %f does not match iterator count %f", top->vertexCount, vertexCount);
    }

    message("%u graph vertices ok", (guint) top->vertexCount);

    return TRUE;
}

static void _topology_checkGraphEdgesHelperHook(Topology* top, igraph_integer_t edgeIndex, gpointer userData) {
    MAGIC_ASSERT(top);

    igraph_integer_t fromVertexIndex, toVertexIndex;
    gint result = igraph_edge(&top->graph, edgeIndex, &fromVertexIndex, &toVertexIndex);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_edge return non-success code %i", result);
        return;
    }

    const gchar* fromIDStr = VAS(&top->graph, "id", fromVertexIndex);
    const gchar* toIDStr = VAS(&top->graph, "id", toVertexIndex);

    /* get edge attributes: S for string and N for numeric */
    igraph_real_t latency = EAN(&top->graph, "latency", edgeIndex);
    igraph_real_t jitter = EAN(&top->graph, "jitter", edgeIndex);
    igraph_real_t ploss = EAN(&top->graph, "packetloss", edgeIndex);

    if(latency <= 0) {
        error("invalid latency %f on edge %li from vertex %li (%s) to vertex %li (%s)",
            latency, (glong)edgeIndex, (glong)fromVertexIndex, fromIDStr, (glong)toVertexIndex, toIDStr);
    }

    utility_assert(latency > 0);

    debug("found edge %li from vertex %li (%s) to vertex %li (%s) latency=%f jitter=%f packetloss=%f",
            (glong)edgeIndex, (glong)fromVertexIndex, fromIDStr, (glong)toVertexIndex, toIDStr,
            latency, jitter, ploss);
}

static igraph_integer_t _topology_iterateAllEdges(Topology* top, EdgeNotifyFunc hook, gpointer userData) {
    MAGIC_ASSERT(top);
    utility_assert(hook);

    /* we will iterate through the edges */
    igraph_eit_t edgeIterator;
    gint result = igraph_eit_create(&top->graph, igraph_ess_all(IGRAPH_EDGEORDER_ID), &edgeIterator);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_eit_create return non-success code %i", result);
        return -1;
    }

    /* count the edges as we iterate */
    igraph_integer_t edgeCount = 0;
    while (!IGRAPH_EIT_END(edgeIterator)) {
        long int edgeIndex = IGRAPH_EIT_GET(edgeIterator);

        /* call the hook function for each edge */
        hook(top, (igraph_integer_t) edgeIndex, userData);

        edgeCount++;
        IGRAPH_EIT_NEXT(edgeIterator);
    }

    igraph_eit_destroy(&edgeIterator);

    return edgeCount;
}

static gboolean _topology_checkGraphEdges(Topology* top) {
    MAGIC_ASSERT(top);

    message("checking graph edges...");

    igraph_integer_t edgeCount = _topology_iterateAllEdges(top, _topology_checkGraphEdgesHelperHook, NULL);
    if(edgeCount < 0) {
        /* there was some kind of error */
        return FALSE;
    }

    top->edgeCount = igraph_ecount(&top->graph);
    if(top->edgeCount != edgeCount) {
        warning("igraph_vcount %f does not match iterator count %f", top->edgeCount, edgeCount);
    }

    message("%u graph edges ok", (guint) top->edgeCount);

    return TRUE;
}

static gboolean _topology_checkGraph(Topology* top) {
    gboolean isSuccess = FALSE;

    g_mutex_lock(&(top->topologyLock));
    _topology_lockGraph(top);

    if(!_topology_checkGraphProperties(top) || !_topology_checkGraphVertices(top) ||
            !_topology_checkGraphEdges(top)) {
        isSuccess = FALSE;
    } else {
        isSuccess = TRUE;
        message("successfully parsed graphml and validated topology: "
                "graph is %s with %u %s, %u %s, and %u %s",
                top->isConnected ? "strongly connected" : "disconnected",
                (guint)top->clusterCount, top->clusterCount == 1 ? "cluster" : "clusters",
                (guint)top->vertexCount, top->vertexCount == 1 ? "vertex" : "vertices",
                (guint)top->edgeCount, top->edgeCount == 1 ? "edge" : "edges");
    }

    _topology_unlockGraph(top);
    g_mutex_unlock(&(top->topologyLock));

    return isSuccess;
}

static gboolean _topology_extractEdgeWeights(Topology* top) {
    MAGIC_ASSERT(top);

    _topology_lockGraph(top);
    g_rw_lock_writer_lock(&(top->edgeWeightsLock));

    /* create new or clear existing edge weights */
    if(!top->edgeWeights) {
        top->edgeWeights = g_new0(igraph_vector_t, 1);
    } else {
        igraph_vector_destroy(top->edgeWeights);
        memset(top->edgeWeights, 0, sizeof(igraph_vector_t));
    }

    /* now we have fresh memory */
    gint result = igraph_vector_init(top->edgeWeights, (glong) top->edgeCount);
    if(result != IGRAPH_SUCCESS) {
        g_rw_lock_writer_unlock(&(top->edgeWeightsLock));
        _topology_unlockGraph(top);
        critical("igraph_vector_init return non-success code %i", result);
        return FALSE;
    }

    /* use the 'latency' edge attribute as the edge weight */
    result = EANV(&top->graph, "latency", top->edgeWeights);
    g_rw_lock_writer_unlock(&(top->edgeWeightsLock));
    _topology_unlockGraph(top);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_cattribute_EANV return non-success code %i", result);
        return FALSE;
    }

    return TRUE;
}

static void _topology_clearCache(Topology* top) {
    MAGIC_ASSERT(top);
    g_rw_lock_writer_lock(&(top->pathCacheLock));
    if(top->pathCache) {
        g_hash_table_destroy(top->pathCache);
        top->pathCache = NULL;
    }
    g_rw_lock_writer_unlock(&(top->pathCacheLock));

    /* lock the read on the shortest path info */
    g_mutex_lock(&(top->topologyLock));
    message("path cache cleared, spent %f seconds computing %u shortest paths",
            top->shortestPathTotalTime, top->shortestPathCount);
    g_mutex_unlock(&(top->topologyLock));
}

static Path* _topology_getPathFromCache(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_integer_t dstVertexIndex) {
    MAGIC_ASSERT(top);

    Path* path = NULL;
    g_rw_lock_reader_lock(&(top->pathCacheLock));

    if(top->pathCache) {
        /* look for the source first level cache */
        gpointer sourceCache = g_hash_table_lookup(top->pathCache, GINT_TO_POINTER(srcVertexIndex));

        if(sourceCache) {
            /* check for the path to destination in source cache */
            path = g_hash_table_lookup(sourceCache, GINT_TO_POINTER(dstVertexIndex));
        }
    }

    g_rw_lock_reader_unlock(&(top->pathCacheLock));

    /* NULL if cache miss */
    return path;
}

static void _topology_storePathInCache(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_integer_t dstVertexIndex, igraph_real_t totalLatency, igraph_real_t totalReliability) {
    MAGIC_ASSERT(top);

    gdouble latencyMS = (gdouble) totalLatency;
    gboolean wasUpdated = FALSE;

    Path* path = path_new(latencyMS, (gdouble) totalReliability);

    g_rw_lock_writer_lock(&(top->pathCacheLock));

    /* create latency cache on the fly */
    if(!top->pathCache) {
        /* stores hash tables for source address caches */
        top->pathCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_hash_table_destroy);
    }

    GHashTable* sourceCache = g_hash_table_lookup(top->pathCache, GINT_TO_POINTER(srcVertexIndex));
    if(!sourceCache) {
        /* dont have a cache for this source yet, create one now */
        sourceCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)path_free);
        g_hash_table_replace(top->pathCache, GINT_TO_POINTER(srcVertexIndex), sourceCache);
    }

    /* now cache this sources path to the destination */
    g_hash_table_replace(sourceCache, GINT_TO_POINTER(dstVertexIndex), path);

    /* track the minimum network latency in the entire graph */
    if(top->minimumPathLatency == 0 || latencyMS < top->minimumPathLatency) {
        top->minimumPathLatency = latencyMS;
        wasUpdated = TRUE;
    }

    g_rw_lock_writer_unlock(&(top->pathCacheLock));

    /* make sure the worker knows the new min latency */
    if(wasUpdated) {
        worker_updateMinTimeJump(top->minimumPathLatency);
    }
}

static igraph_integer_t _topology_getConnectedVertexIndex(Topology* top, Address* address) {
    MAGIC_ASSERT(top);

    /* find the vertex where this virtual ip was attached */
    gpointer vertexIndexPtr = NULL;
    in_addr_t ip = address_toNetworkIP(address);

    g_rw_lock_reader_lock(&(top->virtualIPLock));
    gboolean found = g_hash_table_lookup_extended(top->virtualIP, GUINT_TO_POINTER(ip), NULL, &vertexIndexPtr);
    g_rw_lock_reader_unlock(&(top->virtualIPLock));

    if(!found) {
        warning("address %s is not connected to the topology", address_toHostIPString(address));
        return (igraph_integer_t) -1;
    }

    return (igraph_integer_t) GPOINTER_TO_INT(vertexIndexPtr);
}

/* @warning top->graphLock must be held when calling this function!! */
static gint _topology_getEdgeHelper(Topology* top, igraph_integer_t fromVertexIndex,
        igraph_integer_t toVertexIndex, igraph_real_t* edgeLatencyOut, igraph_real_t* edgeReliabilityOut) {
    MAGIC_ASSERT(top);

    igraph_integer_t edgeIndex = 0;

#ifndef IGRAPH_VERSION
    gint result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, (igraph_bool_t)TRUE);
#else
    gint result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, (igraph_bool_t)TRUE, (igraph_bool_t)TRUE);
#endif

    if(result != IGRAPH_SUCCESS) {
        return result;
    }

    /* get edge properties from graph */
    if(edgeLatencyOut) {
        *edgeLatencyOut = EAN(&top->graph, "latency", edgeIndex);
    }
    if(edgeReliabilityOut) {
        *edgeReliabilityOut = 1.0f - EAN(&top->graph, "packetloss", edgeIndex);
    }

    return IGRAPH_SUCCESS;
}

static gboolean _topology_computeSourcePathsHelper(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_vector_t* resultPathVertices) {
    MAGIC_ASSERT(top);

    /* each position represents a single destination.
     * this resultPathVertices vector holds the links that form the shortest path to this destination.
     * the destination vertex is the last vertex in the vector.
     *
     * there are multiple chances to drop a packet here:
     * psrc : loss rate from source vertex
     * plink ... : loss rate on the links between source-vertex and destination-vertex
     * pdst : loss rate from destination vertex
     *
     * The reliability is then the combination of the probability
     * that its not dropped in each case:
     * P = ((1-psrc)(1-plink)...(1-pdst))
     */
    gint result = 0;
    igraph_real_t totalLatency = 0.0;
    igraph_real_t totalReliability = (igraph_real_t) 1;

    igraph_integer_t dstVertexIndex = (igraph_integer_t) -1;
    const gchar* dstIDStr = NULL;
    const gchar* srcIDStr = NULL;
    GString* pathString = g_string_new(NULL);

    glong nVertices = igraph_vector_size(resultPathVertices);

    _topology_lockGraph(top);

    /* get source properties */
    totalReliability *= (1.0f - VAN(&top->graph, "packetloss", srcVertexIndex));
    srcIDStr = VAS(&top->graph, "id", srcVertexIndex);
    g_string_append_printf(pathString, "%s", srcIDStr);

    if(nVertices == 0) {
        /* src and dst are attached to the same poi vertex */
        totalLatency = 1.0;
        dstVertexIndex = srcVertexIndex;
        dstIDStr = srcIDStr;
    } else {
        /* get destination properties */
        dstVertexIndex = (igraph_integer_t) igraph_vector_tail(resultPathVertices);
        dstIDStr = VAS(&top->graph, "id", dstVertexIndex);

        /* only include dst loss if there is no path between src and dst vertices */
        if((srcVertexIndex != dstVertexIndex) || (srcVertexIndex == dstVertexIndex && nVertices > 2)) {
            totalReliability *= (1.0f - VAN(&top->graph, "packetloss", dstVertexIndex));
        }

        /* the source is in the first position only if we have more than one vertex */
        if(nVertices > 1) {
            utility_assert(srcVertexIndex == igraph_vector_e(resultPathVertices, 0));
        }

        /* if we have only one vertex, its the destination at position 0; otherwise, the source is
         * at position 0 and the part of the path after the source starts at position 1 */
        gint startingPosition = nVertices == 1 ? 0 : 1;

        igraph_integer_t fromVertexIndex = srcVertexIndex, toVertexIndex = 0,  edgeIndex = 0;
        const gchar* fromIDStr = srcIDStr;

        /* now iterate to get latency and reliability from each edge in the path */
        for (gint i = startingPosition; i < nVertices; i++) {
            /* get the edge */
            toVertexIndex = igraph_vector_e(resultPathVertices, i);
            const gchar* toIDStr = VAS(&top->graph, "id", toVertexIndex);

            igraph_real_t edgeLatency = 0, edgeReliability = 0;

            result = _topology_getEdgeHelper(top, fromVertexIndex, toVertexIndex, &edgeLatency, &edgeReliability);
            if(result != IGRAPH_SUCCESS) {
                _topology_unlockGraph(top);
                critical("igraph_get_eid return non-success code %i for edge between "
                         "%s (%i) and %s (%i)", result, fromIDStr, (gint) fromVertexIndex, toIDStr, (gint) toVertexIndex);
                return FALSE;
            }

            /* accumulate path attributes */
            totalLatency += edgeLatency;
            totalReliability *= edgeReliability;

            /* accumulate path information */
            g_string_append_printf(pathString, "--[%f,%f]-->%s", edgeLatency, edgeReliability, toIDStr);

            /* update for next edge */
            fromVertexIndex = toVertexIndex;
            fromIDStr = toIDStr;
        }
    }

    _topology_unlockGraph(top);

    debug("shortest path %s-->%s (%i-->%i) is %f ms with %f loss, path: %s", srcIDStr, dstIDStr,
            (gint) srcVertexIndex, (gint) dstVertexIndex,
            totalLatency, 1-totalReliability, pathString->str);

    if(totalLatency == (igraph_real_t) 0) {
        totalLatency = (igraph_real_t) 1;
        warning("overriding 0 latency to 1 on: shortest path %s-->%s (%i-->%i) is %f ms with %f loss, path: %s",
                srcIDStr, dstIDStr, (gint) srcVertexIndex, (gint) dstVertexIndex,
                totalLatency, 1-totalReliability, pathString->str);
    }

    g_string_free(pathString, TRUE);

    /* cache the latency and reliability we just computed */
    _topology_storePathInCache(top, srcVertexIndex, dstVertexIndex, totalLatency, totalReliability);

    return TRUE;
}

static gboolean _topology_computeSourcePaths(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_integer_t dstVertexIndex) {
    MAGIC_ASSERT(top);
    utility_assert(srcVertexIndex >= 0);
    utility_assert(dstVertexIndex >= 0);

    _topology_lockGraph(top);
    const gchar* srcIDStr = VAS(&top->graph, "id", srcVertexIndex);
    const gchar* dstIDStr = VAS(&top->graph, "id", dstVertexIndex);
    _topology_unlockGraph(top);

    info("requested path between source vertex %li (%s) and destination vertex %li (%s)",
            (glong)srcVertexIndex, srcIDStr, (glong)dstVertexIndex, dstIDStr);

    /* we are going to compute shortest path from the source to all attached destinations
     * (including dstAddress) in order to cut down on the the number of dijkstra runs we do */
    g_rw_lock_reader_lock(&(top->virtualIPLock));
    GList* attachedTargets = g_hash_table_get_values(top->virtualIP);
    g_rw_lock_reader_unlock(&(top->virtualIPLock));

    /* normally we should hold the lock while modifying the list, but since the virtualIPLock
     * hash table stores vertex indices in pointers, this should be OK. */
    guint numTargets = g_list_length(attachedTargets);

    /* initialize vector to hold intended destinations */
    igraph_vector_t dstVertexIndexSet;

    gint result = igraph_vector_init(&dstVertexIndexSet, (long int) numTargets);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_vector_init return non-success code %i", result);
        return FALSE;
    }

    /* initialize our result vector where the resulting paths will be stored */
    igraph_vector_ptr_t resultPaths;
    result = igraph_vector_ptr_init(&resultPaths, (long int) numTargets);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_vector_ptr_init return non-success code %i", result);
        return FALSE;
    }

    gboolean foundDstVertexIndex = FALSE;
    gint dstVertexIndexPosition = -1;
    GList* target = attachedTargets;
    for(gint position = 0; position < numTargets; position++) {
        utility_assert(target != NULL);

        /* set each vertex index as a destination for dijkstra */
        igraph_integer_t vertexIndex = (igraph_integer_t) GPOINTER_TO_INT(target->data);
        igraph_vector_set(&dstVertexIndexSet, position, (igraph_real_t) vertexIndex);
        utility_assert(vertexIndex == igraph_vector_e(&dstVertexIndexSet, position));
        target = target->next;

        if(vertexIndex == dstVertexIndex) {
            foundDstVertexIndex = TRUE;
            dstVertexIndexPosition = position;
        }

        /* initialize a vector to hold the result path vertices for this target */
        igraph_vector_t* resultPathVertices = g_new0(igraph_vector_t, 1);

        /* initialize with 0 entries, since we dont know how long the paths with be */
        result = igraph_vector_init(resultPathVertices, 0);
        if(result != IGRAPH_SUCCESS) {
            critical("igraph_vector_init return non-success code %i", result);
            return FALSE;
        }

        /* assign our element to the result vector */
        igraph_vector_ptr_set(&resultPaths, position, resultPathVertices);
        utility_assert(resultPathVertices == igraph_vector_ptr_e(&resultPaths, position));
    }

    utility_assert(numTargets == igraph_vector_size(&dstVertexIndexSet));
    utility_assert(numTargets == igraph_vector_ptr_size(&resultPaths));
    utility_assert(foundDstVertexIndex == TRUE);

    info("computing shortest paths from source vertex %li (%s) to all connected destinations",
            (glong)srcVertexIndex, srcIDStr);

    _topology_lockGraph(top);
    g_rw_lock_reader_lock(&(top->edgeWeightsLock));

    /* time the dijkstra algorithm */
    GTimer* pathTimer = g_timer_new();

    /* run dijkstra's shortest path algorithm */
#if defined (IGRAPH_VERSION_MAJOR) && defined (IGRAPH_VERSION_MINOR) && defined (IGRAPH_VERSION_PATCH)
#if ((IGRAPH_VERSION_MAJOR == 0 && IGRAPH_VERSION_MINOR >= 7) || IGRAPH_VERSION_MAJOR > 0)
    result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths, NULL,
            srcVertexIndex, igraph_vss_vector(&dstVertexIndexSet), top->edgeWeights, IGRAPH_OUT, NULL, NULL);
#else
    result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths, NULL,
                srcVertexIndex, igraph_vss_vector(&dstVertexIndexSet), top->edgeWeights, IGRAPH_OUT);
#endif
#else
#if defined (IGRAPH_VERSION)
#if defined (IGRAPH_VERSION_MAJOR_GUESS) && defined (IGRAPH_VERSION_MINOR_GUESS) && ((IGRAPH_VERSION_MAJOR_GUESS == 0 && IGRAPH_VERSION_MINOR_GUESS >= 7) || IGRAPH_VERSION_MAJOR_GUESS > 0)
    result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths, NULL,
                srcVertexIndex, igraph_vss_vector(&dstVertexIndexSet), top->edgeWeights, IGRAPH_OUT, NULL, NULL);
#else
    result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths, NULL,
                srcVertexIndex, igraph_vss_vector(&dstVertexIndexSet), top->edgeWeights, IGRAPH_OUT);
#endif
#else
    result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths,
            srcVertexIndex, igraph_vss_vector(&dstVertexIndexSet), top->edgeWeights, IGRAPH_OUT);
#endif
#endif

    /* track the time spent running the algorithm */
    gdouble elapsedSeconds = g_timer_elapsed(pathTimer, NULL);

    g_rw_lock_reader_unlock(&(top->edgeWeightsLock));
    _topology_unlockGraph(top);

    g_timer_destroy(pathTimer);

    g_mutex_lock(&top->topologyLock);
    top->shortestPathTotalTime += elapsedSeconds;
    top->shortestPathCount++;
    g_mutex_unlock(&top->topologyLock);

    if(result != IGRAPH_SUCCESS) {
        critical("igraph_get_shortest_paths_dijkstra return non-success code %i", result);
        return FALSE;
    }

    utility_assert(numTargets == igraph_vector_size(&dstVertexIndexSet));
    utility_assert(numTargets == igraph_vector_ptr_size(&resultPaths));

    gboolean allSuccess = TRUE;
    gboolean foundDstPosition = FALSE;

    /* go through the result paths for all targets */
    for(gint position = 0; position < numTargets; position++) {
        /* handle the path to the destination at this position */
        igraph_vector_t* resultPathVertices = igraph_vector_ptr_e(&resultPaths, position);

        if(dstVertexIndexPosition == position) {
            foundDstPosition = TRUE;
        }

        gboolean success = _topology_computeSourcePathsHelper(top, srcVertexIndex, resultPathVertices);
        if(!success) {
            allSuccess = FALSE;
        }

        igraph_vector_destroy(resultPathVertices);
        g_free(resultPathVertices);
    }

    utility_assert(foundDstPosition == TRUE);

    /* clean up */
    igraph_vector_ptr_destroy(&resultPaths);
    igraph_vector_destroy(&dstVertexIndexSet);

    /* success */
    return allSuccess;
}

static gboolean _topology_lookupPath(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_integer_t dstVertexIndex) {
    MAGIC_ASSERT(top);

    /* for complete graphs, we lookup the edge and use it as the path instead
     * of running the shortest path algorithm.
     *
     * see the comment in _topology_computeSourcePathsHelper
     */

    igraph_real_t totalLatency = 0.0, totalReliability = 1.0;
    igraph_real_t edgeLatency = 0.0, edgeReliability = 1.0;

    _topology_lockGraph(top);

    const gchar* srcIDStr = VAS(&top->graph, "id", srcVertexIndex);
    const gchar* dstIDStr = VAS(&top->graph, "id", dstVertexIndex);

    totalReliability *= (1.0f - VAN(&top->graph, "packetloss", srcVertexIndex));
    totalReliability *= (1.0f - VAN(&top->graph, "packetloss", dstVertexIndex));

    gint result = _topology_getEdgeHelper(top, srcVertexIndex, dstVertexIndex, &edgeLatency, &edgeReliability);
    if(result != IGRAPH_SUCCESS) {
        _topology_unlockGraph(top);
        critical("igraph_get_eid return non-success code %i for edge between "
                 "%s (%i) and %s (%i)", result, srcIDStr, (gint) srcVertexIndex, dstIDStr, (gint) dstVertexIndex);
        return FALSE;
    }

    _topology_unlockGraph(top);

    totalLatency += edgeLatency;
    totalReliability *= edgeReliability;

    /* cache the latency and reliability we just computed */
    _topology_storePathInCache(top, srcVertexIndex, dstVertexIndex, totalLatency, totalReliability);

    return TRUE;
}


static gboolean _topology_getPathEntry(Topology* top, Address* srcAddress, Address* dstAddress,
        gdouble* latency, gdouble* reliability) {
    MAGIC_ASSERT(top);

    /* get connected points */
    igraph_integer_t srcVertexIndex = _topology_getConnectedVertexIndex(top, srcAddress);
    if(srcVertexIndex < 0) {
        critical("invalid vertex %i, source address %s is not connected to topology",
                (gint)srcVertexIndex, address_toString(srcAddress));
        return FALSE;
    }
    igraph_integer_t dstVertexIndex = _topology_getConnectedVertexIndex(top, dstAddress);
    if(dstVertexIndex < 0) {
        critical("invalid vertex %i, destination address %s is not connected to topology",
                (gint)dstVertexIndex, address_toString(dstAddress));
        return FALSE;
    }

    /* check for a cache hit */
    Path* path = _topology_getPathFromCache(top, srcVertexIndex, dstVertexIndex);
    if(!path && !top->isDirected) {
        path = _topology_getPathFromCache(top, dstVertexIndex, srcVertexIndex);
    }

    if(!path) {
        /* cache miss, lets find the path */
        gboolean success = FALSE;

        if(top->isComplete) {
            /* use the edge between src and dst as the path */
            success = _topology_lookupPath(top, srcVertexIndex, dstVertexIndex);
        } else {
            /* use shortest path over the network graph */
            success = _topology_computeSourcePaths(top, srcVertexIndex, dstVertexIndex);
        }

        if(success) {
            path = _topology_getPathFromCache(top, srcVertexIndex, dstVertexIndex);
        }
    }

    if(!path) {
        /* some error finding the path */
        _topology_lockGraph(top);
        const gchar* srcIDStr = VAS(&top->graph, "id", srcVertexIndex);
        const gchar* dstIDStr = VAS(&top->graph, "id", dstVertexIndex);
        _topology_unlockGraph(top);

        error("unable to find path between node %s at %s (vertex %i) "
                "and node %s at %s (vertex %i)",
                address_toString(srcAddress), srcIDStr, (gint)srcVertexIndex,
                address_toString(dstAddress), dstIDStr, (gint)dstVertexIndex);
        return FALSE;
    }

    if(latency) {
        *latency = path_getLatency(path);
    }
    if(reliability) {
        *reliability = path_getReliability(path);
    }
    return TRUE;
}

gdouble topology_getLatency(Topology* top, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(top);
    gdouble latency = 0;
    if(_topology_getPathEntry(top, srcAddress, dstAddress, &latency, NULL)) {
        return latency;
    } else {
        return (gdouble) -1;
    }
}

gdouble topology_getReliability(Topology* top, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(top);
    gdouble reliability = 0;
    if(_topology_getPathEntry(top, srcAddress, dstAddress, NULL, &reliability)) {
        return reliability;
    } else {
        return (gdouble) -1;
    }
}

gboolean topology_isRoutable(Topology* top, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(top);
    return topology_getLatency(top, srcAddress, dstAddress) > -1;
}

static void _topology_findAttachmentVertexHelperHook(Topology* top, igraph_integer_t vertexIndex, AttachHelper* ah) {
    MAGIC_ASSERT(top);
    utility_assert(ah);

    /* @warning: make sure we hold the graph lock when iterating with this helper
     * @todo: this could be made much more efficient */

    const gchar* idStr = VAS(&top->graph, "id", vertexIndex);

    if(g_strstr_len(idStr, (gssize)-1, "poi")) {
        /* first check the IP address */
        const gchar* ipStr = VAS(&top->graph, "ip", vertexIndex);
        in_addr_t vertexIP = address_stringToIP(ipStr);

        gboolean vertexHasUsableIP = FALSE;
        if(vertexIP != INADDR_NONE && vertexIP != INADDR_ANY) {
            vertexHasUsableIP = TRUE;
        }

        /* check for exact IP address match */
        if(ah->ipHint && ah->requestedIP != INADDR_NONE && ah->requestedIP != INADDR_ANY) {
            if(vertexIP == ah->requestedIP) {
                if(!ah->foundExactIPMatch) {
                    /* first time we found a match, clear all queues to make sure we only
                     * select from the matching IP vertices */
                    g_queue_clear(ah->candidatesAll);
                    g_queue_clear(ah->candidatesType);
                    g_queue_clear(ah->candidatesCode);
                    g_queue_clear(ah->candidatesTypeCode);
                }
                ah->foundExactIPMatch = TRUE;
                g_queue_push_tail(ah->candidatesAll, GINT_TO_POINTER(vertexIndex));
                if(vertexHasUsableIP) {
                    ah->numCandidatesAllIPs++;
                }
            }
        }

        /* if it matches the requested IP exactly, we ignore other the filters */
        if(ah->foundExactIPMatch) {
            return;
        }

        const gchar* typeStr = VAS(&top->graph, "type", vertexIndex);
        const gchar* geocodeStr = VAS(&top->graph, "geocode", vertexIndex);

        gboolean typeMatches = ah->typeHint && !g_ascii_strcasecmp(typeStr, ah->typeHint);
        gboolean codeMatches = ah->geocodeHint && !g_ascii_strcasecmp(geocodeStr, ah->geocodeHint);

        g_queue_push_tail(ah->candidatesAll, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numCandidatesAllIPs++;
        }
        if(typeMatches && ah->candidatesType) {
            g_queue_push_tail(ah->candidatesType, GINT_TO_POINTER(vertexIndex));
            if(vertexHasUsableIP) {
                ah->numCandidatesTypeIPs++;
            }
        }
        if(codeMatches && ah->candidatesCode) {
            g_queue_push_tail(ah->candidatesCode, GINT_TO_POINTER(vertexIndex));
            if(vertexHasUsableIP) {
                ah->numCandidatesCodeIPs++;
            }
        }
        if(typeMatches && codeMatches && ah->candidatesTypeCode) {
            g_queue_push_tail(ah->candidatesTypeCode, GINT_TO_POINTER(vertexIndex));
            if(vertexHasUsableIP) {
                ah->numCandidatesTypeCodeIPs++;
            }
        }
    }
}

static igraph_integer_t* _topology_getLongestPrefixMatch(Topology* top, GQueue* vertexSet, in_addr_t ip) {
    MAGIC_ASSERT(top);
    utility_assert(vertexSet);

    /* @warning: this empties the candidate queue */

    in_addr_t bestMatch = 0;
    in_addr_t bestIP = 0;
    igraph_integer_t* bestVertexIndexPtr = GINT_TO_POINTER(-1);

    while(!g_queue_is_empty(vertexSet)) {
        igraph_integer_t* vertexIndexPtr = g_queue_pop_head(vertexSet);
        igraph_integer_t vertexIndex = (igraph_integer_t) GPOINTER_TO_INT(vertexIndexPtr);
        _topology_lockGraph(top);
        const gchar* ipStr = VAS(&top->graph, "ip", vertexIndex);
        _topology_unlockGraph(top);
        in_addr_t vertexIP = address_stringToIP(ipStr);

        in_addr_t match = (vertexIP & ip);
        if(match > bestMatch) {
            bestMatch = match;
            bestIP = vertexIP;
            bestVertexIndexPtr = vertexIndexPtr;
        }
    }

    return bestVertexIndexPtr;
}

static igraph_integer_t _topology_findAttachmentVertex(Topology* top, Random* randomSourcePool,
        in_addr_t nodeIP, gchar* ipHint, gchar* geocodeHint, gchar* typeHint) {
    MAGIC_ASSERT(top);

    igraph_integer_t vertexIndex = (igraph_integer_t) -1;
    igraph_integer_t* vertexIndexPtr = GINT_TO_POINTER(-1);

    AttachHelper* ah = g_new0(AttachHelper, 1);
    ah->geocodeHint = geocodeHint;
    ah->ipHint = ipHint;
    ah->typeHint = typeHint;
    ah->requestedIP = ipHint ? address_stringToIP(ipHint) : INADDR_NONE;
    ah->candidatesAll = g_queue_new();
    ah->candidatesCode = g_queue_new();
    ah->candidatesType = g_queue_new();
    ah->candidatesTypeCode = g_queue_new();

    /* go through the vertices to see which ones match our hint filters */
    _topology_lockGraph(top);
    _topology_iterateAllVertices(top, (VertexNotifyFunc) _topology_findAttachmentVertexHelperHook, ah);
    _topology_unlockGraph(top);

    /* the logic here is to try and find the most specific match following the hints.
     * we always use exact IP hint matches, and otherwise use it to select the best possible
     * match from the final set of candidates. the type and geocode hints are used to filter
     * all vertices down to a smaller set. if that smaller set is empty, then we fall back to the
     * type-only filtered set. if the type-only set is empty, we fall back to the geocode-only
     * filtered set. if that is empty, we stick with the complete vertex set.
     */
    GQueue* candidates = NULL;
    gboolean useLongestPrefixMatching = FALSE;

    if(g_queue_get_length(ah->candidatesTypeCode) > 0) {
        candidates = ah->candidatesTypeCode;
        useLongestPrefixMatching = (ipHint && ah->numCandidatesTypeCodeIPs > 0);
    } else if(g_queue_get_length(ah->candidatesType) > 0) {
        candidates = ah->candidatesType;
        useLongestPrefixMatching = (ipHint && ah->numCandidatesTypeIPs > 0);
    } else if(g_queue_get_length(ah->candidatesCode) > 0) {
        candidates = ah->candidatesCode;
        useLongestPrefixMatching = (ipHint && ah->numCandidatesCodeIPs > 0);
    } else {
        candidates = ah->candidatesAll;
        useLongestPrefixMatching = (ipHint && ah->numCandidatesAllIPs > 0);
    }

    guint numCandidates = g_queue_get_length(candidates);
    utility_assert(numCandidates > 0);

    /* if our candidate list has vertices with non-zero IPs, use longest prefix matching
     * to select the closest one to the requested IP; otherwise, grab a random candidate */
    if(useLongestPrefixMatching && !ah->foundExactIPMatch) {
        vertexIndexPtr = _topology_getLongestPrefixMatch(top, candidates, ah->requestedIP);
    } else {
        gdouble randomDouble = random_nextDouble(randomSourcePool);
        gint indexRange = numCandidates - 1;
        gint chosenIndex = (gint) round((gdouble)(indexRange * randomDouble));
        while(chosenIndex >= 0) {
            vertexIndexPtr = g_queue_pop_head(candidates);
            chosenIndex--;
        }
    }

    /* make sure the vertex we found is legitimate */
    utility_assert(vertexIndexPtr != GINT_TO_POINTER(-1));
    vertexIndex = (igraph_integer_t) GPOINTER_TO_INT(vertexIndexPtr);
    utility_assert(vertexIndex > (igraph_integer_t) -1);

    /* clean up */
    if(ah->candidatesAll) {
        g_queue_free(ah->candidatesAll);
    }
    if(ah->candidatesCode) {
        g_queue_free(ah->candidatesCode);
    }
    if(ah->candidatesType) {
        g_queue_free(ah->candidatesType);
    }
    if(ah->candidatesTypeCode) {
        g_queue_free(ah->candidatesTypeCode);
    }
    g_free(ah);

    return vertexIndex;
}

void topology_attach(Topology* top, Address* address, Random* randomSourcePool,
        gchar* ipHint, gchar* geocodeHint, gchar* typeHint, guint64* bwDownOut, guint64* bwUpOut) {
    MAGIC_ASSERT(top);
    utility_assert(address);

    in_addr_t nodeIP = address_toNetworkIP(address);
    igraph_integer_t vertexIndex = _topology_findAttachmentVertex(top, randomSourcePool, nodeIP, ipHint,
            geocodeHint, typeHint);

    /* attach it, i.e. store the mapping so we can route later */
    g_rw_lock_writer_lock(&(top->virtualIPLock));
    g_hash_table_replace(top->virtualIP, GUINT_TO_POINTER(nodeIP), GINT_TO_POINTER(vertexIndex));
    g_rw_lock_writer_unlock(&(top->virtualIPLock));

    _topology_lockGraph(top);

    /* give them the default cluster bandwidths if they asked */
    if(bwUpOut) {
        *bwUpOut = (guint64) VAN(&top->graph, "bandwidthup", vertexIndex);
    }
    if(bwDownOut) {
        *bwDownOut = (guint64) VAN(&top->graph, "bandwidthdown", vertexIndex);
    }

    const gchar* idStr = VAS(&top->graph, "id", vertexIndex);
    const gchar* typeStr = VAS(&top->graph, "type", vertexIndex);
    const gchar* ipStr = VAS(&top->graph, "ip", vertexIndex);
    const gchar* geocodeStr = VAS(&top->graph, "geocode", vertexIndex);

    _topology_unlockGraph(top);

    info("connected address '%s' to point of interest '%s' (ip=%s, geocode=%s, type=%s) "
            "using hints (ip=%s, geocode=%s, type=%s)", address_toHostIPString(address), idStr,
            ipStr, geocodeStr, typeStr, ipHint, geocodeHint, typeHint);
}

void topology_detach(Topology* top, Address* address) {
    MAGIC_ASSERT(top);
    in_addr_t ip = address_toNetworkIP(address);

    g_rw_lock_writer_lock(&(top->virtualIPLock));
    g_hash_table_remove(top->virtualIP, GUINT_TO_POINTER(ip));
    g_rw_lock_writer_unlock(&(top->virtualIPLock));
}

void topology_free(Topology* top) {
    MAGIC_ASSERT(top);

    /* clear the virtual ip table */
    g_rw_lock_writer_lock(&(top->virtualIPLock));
    if(top->virtualIP) {
        g_hash_table_destroy(top->virtualIP);
        top->virtualIP = NULL;
    }
    g_rw_lock_writer_unlock(&(top->virtualIPLock));
    g_rw_lock_clear(&(top->virtualIPLock));

    /* this functions grabs and releases the pathCache write lock */
    _topology_clearCache(top);
    g_rw_lock_clear(&(top->pathCacheLock));

    /* clear the stored edge weights */
    g_rw_lock_writer_lock(&(top->edgeWeightsLock));
    if(top->edgeWeights) {
        igraph_vector_destroy(top->edgeWeights);
        g_free(top->edgeWeights);
        top->edgeWeights = NULL;
    }
    g_rw_lock_writer_unlock(&(top->edgeWeightsLock));
    g_rw_lock_clear(&(top->edgeWeightsLock));

    /* clear the graph */
    _topology_lockGraph(top);
    igraph_destroy(&(top->graph));
    _topology_unlockGraph(top);
    _topology_clearGraphLock(&(top->graphLock));

    g_mutex_clear(&(top->topologyLock));

    MAGIC_CLEAR(top);
    g_free(top);
}

Topology* topology_new(const gchar* graphPath) {
    utility_assert(graphPath);
    Topology* top = g_new0(Topology, 1);
    MAGIC_INIT(top);

    top->virtualIP = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    _topology_initGraphLock(&(top->graphLock));
    g_mutex_init(&(top->topologyLock));
    g_rw_lock_init(&(top->edgeWeightsLock));
    g_rw_lock_init(&(top->virtualIPLock));
    g_rw_lock_init(&(top->pathCacheLock));

    /* first read in the graph and make sure its formed correctly,
     * then setup our edge weights for shortest path */
    if(!_topology_loadGraph(top, graphPath) || !_topology_checkGraph(top) ||
            !_topology_extractEdgeWeights(top)) {
        topology_free(top);
        return NULL;
    }

    return top;
}
