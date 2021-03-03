/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <glib.h>
#include <igraph.h>
#include <math.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/routing/address.h"
#include "main/routing/path.h"
#include "main/routing/topology.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

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
    GHashTable* verticesWithAttachedHosts;
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
    /* Also a graph property. Normally when a graph is not complete, Shadow
     * will always do shortest path to get from A to B, even if a path from A
     * to B already exists. Sometimes ACB is shorter than AB.
     *
     * If this is true and the graph is not complete, then when Shadow needs to
     * route from A to B, it will prefer to use AB (if it exists) even if it
     * could do shortest path to determine ACB is shorter. */
    gboolean prefersDirectPaths;

    /* keep track of how many, and how long we spend computing shortest paths */
#ifdef USE_PERF_TIMERS
    gdouble shortestPathTotalTime;
    gdouble selfPathTotalTime;
#endif
    guint shortestPathCount;
    guint selfPathCount;

    /* END global topology lock */
    /******/

    MAGIC_DECLARE;
};

typedef enum _GraphAttribute GraphAttribute;
enum _GraphAttribute {
    GRAPH_ATTR_PREFERDIRECTPATHS=1,
};

typedef enum _VertexAttribute VertexAttribute;
enum _VertexAttribute {
    VERTEX_ATTR_ID=2,
    VERTEX_ATTR_BANDWIDTHDOWN=3,
    VERTEX_ATTR_BANDWIDTHUP=4,
    VERTEX_ATTR_IP=5,
    VERTEX_ATTR_CITYCODE=6,
    VERTEX_ATTR_COUNTRYCODE=7,
    VERTEX_ATTR_ASN=8,
    VERTEX_ATTR_TYPE=9,
    VERTEX_ATTR_PACKETLOSS=10,
    VERTEX_ATTR_GEOCODE=11, // deprecated
};

typedef enum _EdgeAttribute EdgeAttribute;
enum _EdgeAttribute {
    EDGE_ATTR_LATENCY=12,
    EDGE_ATTR_PACKETLOSS=13,
    EDGE_ATTR_JITTER=14,
};

typedef struct _AttachHelper AttachHelper;
struct _AttachHelper {
    /* these are ordered by preference, more specific is better */
    GQueue* candidatesCityAndType;
    GQueue* candidatesCity;
    GQueue* candidatesCountryAndType;
    GQueue* candidatesCountry;
    GQueue* candidatesGeoAndType;
    GQueue* candidatesGeo;
    GQueue* candidatesType;
    GQueue* candidatesAll;

    guint numIPsCityAndType;
    guint numIPsCity;
    guint numIPsCountryAndType;
    guint numIPsCountry;
    guint numIPsGeoAndType;
    guint numIPsGeo;
    guint numIPsType;
    guint numIPsAll;

    gchar* ipHint;
    gchar* citycodeHint;
    gchar* countrycodeHint;
    gchar* geocodeHint;
    gchar* typeHint;

    in_addr_t requestedIP;
    gboolean requestedIPIsUsable;

    gboolean foundExactIPMatch;
};

typedef gboolean (*EdgeNotifyFunc)(Topology* top, igraph_integer_t edgeIndex, gpointer userData);
typedef gboolean (*VertexNotifyFunc)(Topology* top, igraph_integer_t vertexIndex, gpointer userData);

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

static const gchar* _topology_igraphAttributeTypeToString(igraph_attribute_type_t type) {
    if(type == IGRAPH_ATTRIBUTE_DEFAULT) {
        return "DEFAULT";
    } else if(type == IGRAPH_ATTRIBUTE_BOOLEAN) {
        return "BOOLEAN";
    } else if(type == IGRAPH_ATTRIBUTE_NUMERIC) {
        return "NUMERIC";
    } else if(type == IGRAPH_ATTRIBUTE_STRING) {
        return "STRING";
    } else {
        return "UNKOWN";
    }
}

static const gchar* _topology_graphAttributeToString(GraphAttribute attr) {
    if(attr == GRAPH_ATTR_PREFERDIRECTPATHS) {
        return "preferdirectpaths";
    } else {
        return "unknown";
    }
}

static gint _topology_graphAttributeLength(GraphAttribute attr) {
    if(attr == GRAPH_ATTR_PREFERDIRECTPATHS) {
        return 17;
    } else {
        return 7;
    }
}

static gboolean _topology_isValidGraphAttributeKey(const gchar* attrName, GraphAttribute attr) {
    gint r = g_ascii_strncasecmp(attrName, _topology_graphAttributeToString(attr), _topology_graphAttributeLength(attr));
    return (r == 0) ? TRUE : FALSE;
}

static const gchar* _topology_vertexAttributeToString(VertexAttribute attr) {
    if(attr == VERTEX_ATTR_ID) {
        return "id";
    } else if(attr == VERTEX_ATTR_BANDWIDTHDOWN) {
        return "bandwidthdown";
    } else if(attr == VERTEX_ATTR_BANDWIDTHUP) {
        return "bandwidthup";
    } else if(attr == VERTEX_ATTR_IP) {
        return "ip";
    } else if(attr == VERTEX_ATTR_CITYCODE) {
        return "citycode";
    } else if(attr == VERTEX_ATTR_COUNTRYCODE) {
        return "countrycode";
    } else if(attr == VERTEX_ATTR_ASN) {
        return "asn";
    } else if(attr == VERTEX_ATTR_TYPE) {
        return "type";
    } else if(attr == VERTEX_ATTR_PACKETLOSS) {
        return "packetloss";
    } else if(attr == VERTEX_ATTR_GEOCODE) {
        return "geocode";
    } else {
        return "unknown";
    }
}

static gint _topology_vertexAttributeLength(VertexAttribute attr) {
    if(attr == VERTEX_ATTR_ID) {
        return 2;
    } else if(attr == VERTEX_ATTR_BANDWIDTHDOWN) {
        return 13;
    } else if(attr == VERTEX_ATTR_BANDWIDTHUP) {
        return 11;
    } else if(attr == VERTEX_ATTR_IP) {
        return 2;
    } else if(attr == VERTEX_ATTR_CITYCODE) {
        return 8;
    } else if(attr == VERTEX_ATTR_COUNTRYCODE) {
        return 11;
    } else if(attr == VERTEX_ATTR_ASN) {
        return 3;
    } else if(attr == VERTEX_ATTR_TYPE) {
        return 4;
    } else if(attr == VERTEX_ATTR_PACKETLOSS) {
        return 10;
    } else if(attr == VERTEX_ATTR_GEOCODE) {
        return 7;
    } else {
        return 7;
    }
}

static gboolean _topology_isValidVertexAttributeKey(const gchar* attrName, VertexAttribute attr) {
    gint r = g_ascii_strncasecmp(attrName, _topology_vertexAttributeToString(attr), _topology_vertexAttributeLength(attr));
    return (r == 0) ? TRUE : FALSE;
}

static const gchar* _topology_edgeAttributeToString(EdgeAttribute attr) {
    if(attr == EDGE_ATTR_LATENCY) {
        return "latency";
    } else if(attr == EDGE_ATTR_PACKETLOSS) {
        return "packetloss";
    } else if(attr == EDGE_ATTR_JITTER) {
        return "jitter";
    } else {
        return "unknown";
    }
}

static gint _topology_edgeAttributeLength(EdgeAttribute attr) {
    if(attr == EDGE_ATTR_LATENCY) {
        return 7;
    } else if(attr == EDGE_ATTR_PACKETLOSS) {
        return 10;
    } else if(attr == EDGE_ATTR_JITTER) {
        return 6;
    } else {
        return 7;
    }
}

static gboolean _topology_isValidEdgeAttributeKey(const gchar* attrName, EdgeAttribute attr) {
    gint r = g_ascii_strncasecmp(attrName, _topology_edgeAttributeToString(attr), _topology_edgeAttributeLength(attr));
    return (r == 0) ? TRUE : FALSE;
}

/* the graph lock should be held when calling this function, since it accesses igraph.
 * if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _topology_findGraphAttributeString(Topology* top, GraphAttribute attr, const gchar** valueOut) {
    MAGIC_ASSERT(top);

    const gchar* name = _topology_graphAttributeToString(attr);

    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_GRAPH, name)) {
        const gchar* value = igraph_cattribute_GAS(&top->graph, name);
        if(value != NULL && value[0] != '\0') {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* the graph lock should be held when calling this function, since it accesses igraph.
 * if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _topology_findVertexAttributeString(Topology* top, igraph_integer_t vertexIndex,
        VertexAttribute attr, const gchar** valueOut) {
    MAGIC_ASSERT(top);

    const gchar* name = _topology_vertexAttributeToString(attr);

    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, name)) {
        const gchar* value = igraph_cattribute_VAS(&top->graph, name, vertexIndex);
        if(value != NULL && value[0] != '\0') {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* the graph lock should be held when calling this function, since it accesses igraph.
 * if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _topology_findVertexAttributeDouble(Topology* top, igraph_integer_t vertexIndex,
        VertexAttribute attr, gdouble* valueOut) {
    MAGIC_ASSERT(top);

    const gchar* name = _topology_vertexAttributeToString(attr);

    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, name)) {
        gdouble value = (gdouble) igraph_cattribute_VAN(&top->graph, name, vertexIndex);
        if(isnan(value) == 0) {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* the graph lock should be held when calling this function, since it accesses igraph.
 * if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _topology_findEdgeAttributeDouble(Topology* top, igraph_integer_t edgeIndex,
        EdgeAttribute attr, gdouble* valueOut) {
    MAGIC_ASSERT(top);

    const gchar* name = _topology_edgeAttributeToString(attr);

    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_EDGE, name)) {
        gdouble value = (gdouble) igraph_cattribute_EAN(&top->graph, name, edgeIndex);
        if(isnan(value) == 0) {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

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

/* @warning top->graphLock must be held when calling this function!! */
static gint _topology_getEdgeHelper(Topology* top,
        igraph_integer_t fromVertexIndex, igraph_integer_t toVertexIndex,
        igraph_integer_t* edgeIndexOut, igraph_real_t* edgeLatencyOut, igraph_real_t* edgeReliabilityOut) {
    MAGIC_ASSERT(top);

    /* directedness of the graph edge should be ignored for undirected graphs */
    igraph_bool_t directedness = (igraph_bool_t) (top->isDirected ? IGRAPH_DIRECTED : IGRAPH_UNDIRECTED);

    /* if FALSE, igraph sets edgeIndex=-1 upon error instead of logging an error */
    igraph_bool_t shouldReportError = (igraph_bool_t) FALSE;

    /* set to -1 so that we are consistent in both versions of igraph_get_eid in case of error */
    igraph_integer_t edgeIndex = -1;

#ifndef IGRAPH_VERSION
    gint result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, directedness);
#else
    gint result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, directedness, shouldReportError);
#endif

    if(result != IGRAPH_SUCCESS) {
        return result;
    }

    /* get edge properties from graph */
    if(edgeLatencyOut) {
        gdouble edgeLatency;
        gdouble found = _topology_findEdgeAttributeDouble(top, edgeIndex, EDGE_ATTR_LATENCY, &edgeLatency);
        utility_assert(found);
        *edgeLatencyOut = edgeLatency;
    }
    if(edgeReliabilityOut) {
        gdouble edgePacketLoss;
        gdouble found = _topology_findEdgeAttributeDouble(top, edgeIndex, EDGE_ATTR_PACKETLOSS, &edgePacketLoss);
        utility_assert(found);
        *edgeReliabilityOut = (1.0f - edgePacketLoss);
    }
    if(edgeIndexOut) {
        *edgeIndexOut = edgeIndex;
    }

    return IGRAPH_SUCCESS;
}

/** Returns FALSE if issue parsing graph, otherwise returns TRUE.
 * If returning FALSE, consider result to be undefined.
 * If returning TRUE, whether or not the graph is complete is stored in result.
 */
static gboolean _topology_isComplete(Topology* top, gboolean *result) {
    MAGIC_ASSERT(top);
    g_assert(result);

    igraph_t *graph = &top->graph;
    igraph_vs_t vs;
    igraph_vit_t vit;
    int ret = 0;
    igraph_integer_t vcount = igraph_vcount(graph);
    igraph_bool_t is_directed = igraph_is_directed(&top->graph);
    gboolean is_success = FALSE;
    gboolean is_complete = FALSE;

    /*
     * Determines if a graph is complete by:
     * - knowning how many vertexes there are
     * - for each vertex, count the indcident edges
     *   - if less than the number of vertexes, it isn't a complete graph
     * - otherwise the graph is complete
     *
     * Notice: In order to be considered complete, every vertex must have an
     * edge beginning and ending at itself too.
     */
    /* vert selector. We wall all verts */
    ret = igraph_vs_all(&vs);
    if (ret != IGRAPH_SUCCESS) {
        critical("igraph_vs_all returned non-success code %i", ret);
        is_success = FALSE;
        goto done;
    }

    ret = igraph_vit_create(graph, vs, &vit);
    if (ret != IGRAPH_SUCCESS) {
        critical("igraph_vit_create returned non-success code %i", ret);
        is_success = FALSE;
        goto done;
    }

    while (!IGRAPH_VIT_END(vit)) {
        igraph_integer_t vertexID = 0;
        vertexID = IGRAPH_VIT_GET(vit);

        igraph_vector_t iedges;
        igraph_vector_init(&iedges, 0);

        ret = igraph_incident(graph, &iedges, vertexID, IGRAPH_OUT);
        if (ret != IGRAPH_SUCCESS) {
            critical("error computing igraph_incident\n");
            is_success = FALSE;
            igraph_vector_destroy(&iedges);
            goto done;
        }

        igraph_integer_t ecount = igraph_vector_size(&iedges);

        /* If the graph is undirected and there is a self-loop edge (an edge
         * that begins and ends at the same vertex) on this vertex, then igraph
         * will have double counted it and we need to correct that. */
        if (!is_directed) {
            igraph_integer_t edge_id = 0;

            gint result = _topology_getEdgeHelper(top, vertexID, vertexID, &edge_id, NULL, NULL);

            /* If the edge does not exist, then -1 will be stored in edge_id.
             * If it is found, then it will be >= 0 */
            if (result == IGRAPH_SUCCESS && edge_id >= 0) {
                debug("Subtracting one from vert id=%li's edge count because "
                        "this is an undirected graph and this vertex's "
                        "self-looping edge has been counted twice", (long int)vertexID);
                ecount -= 1;
            }
        }

        if (ecount < vcount) {
            info("Vert id=%li has %li incident edges to %li total verts "
                "and thus this isn't a complete graph",
                (long int)vertexID, (long int)ecount, (long int)vcount);
            is_success = TRUE;
            is_complete = FALSE;
            igraph_vector_destroy(&iedges);
            goto done;
        } else {
            debug("Vert id=%li has %li incident edges to %li total verts "
                "and thus doesn't determine whether this graph is incomplete. "
                "Must keep searching.", (long int)vertexID,
                (long int)ecount, (long int)vcount);
        }

        igraph_vector_destroy(&iedges);

        IGRAPH_VIT_NEXT(vit);
    }

    info("Determined this graph is complete.");
    is_complete = TRUE;
    is_success = TRUE;

done:
    igraph_vs_destroy(&vs);
    igraph_vit_destroy(&vit);
    *result = is_complete;
    return is_success;
}

static gboolean _topology_checkAttributeType(gchar* parsedName, igraph_attribute_type_t parsedType, igraph_attribute_type_t requiredType) {
    if(parsedType == requiredType) {
        info("graph attribute '%s' with type '%s' is supported", parsedName, _topology_igraphAttributeTypeToString(parsedType));
        return TRUE;
    } else {
        warning("graph attribute '%s' with type '%s' is supported, but we found unsupported type '%s'",
                parsedName, _topology_igraphAttributeTypeToString(requiredType), _topology_igraphAttributeTypeToString(parsedType));
        return FALSE;
    }
}

static gboolean _topology_checkGraphAttributes(Topology* top) {
    MAGIC_ASSERT(top);

    gboolean isSuccess = TRUE;

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

    gint result = igraph_cattribute_list(&top->graph, &gnames, &gtypes, &vnames, &vtypes, &enames, &etypes);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_cattribute_list return non-success code %i", result);
        isSuccess = FALSE;
        goto cleanup;
    }

    gint i = 0;
    gchar* name = NULL;
    igraph_attribute_type_t type = 0;

    /* check all provided graph attributes */
    for(i = 0; i < igraph_strvector_size(&gnames); i++) {
        name = NULL;
        igraph_strvector_get(&gnames, (glong) i, &name);
        type = (igraph_attribute_type_t)igraph_vector_e(&gtypes, (glong) i);

        debug("found graph attribute '%s' with type '%s'", name, _topology_igraphAttributeTypeToString(type));

        if(_topology_isValidGraphAttributeKey(name, GRAPH_ATTR_PREFERDIRECTPATHS)) {
            /* we use a string because there is an error in igraph boolean attribute code. */
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_STRING);
        } else {
            warning("graph attribute '%s' is unsupported and will be ignored", name);
        }
    }

    /* we dont have required graph attributes (yet) */

    /* check all provided vertex attributes */
    for(i = 0; i < igraph_strvector_size(&vnames); i++) {
        name = NULL;
        igraph_strvector_get(&vnames, (glong) i, &name);
        type = igraph_vector_e(&vtypes, (glong) i);

        debug("found vertex attribute '%s' with type '%s'", name, _topology_igraphAttributeTypeToString(type));

        if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_ID)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_STRING);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_IP)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_STRING);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_CITYCODE)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_STRING);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_COUNTRYCODE)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_STRING);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_ASN)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_NUMERIC);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_TYPE)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_STRING);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_BANDWIDTHDOWN)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_NUMERIC);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_BANDWIDTHUP)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_NUMERIC);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_PACKETLOSS)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_NUMERIC);
        } else if(_topology_isValidVertexAttributeKey(name, VERTEX_ATTR_GEOCODE)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_STRING);
            warning("vertex attribute '%s' has been renamed to '%s' and is considered deprecated; "
                    "please use '%s' and/or '%s' instead", name,
                    _topology_vertexAttributeToString(VERTEX_ATTR_COUNTRYCODE),
                    _topology_vertexAttributeToString(VERTEX_ATTR_COUNTRYCODE),
                    _topology_vertexAttributeToString(VERTEX_ATTR_CITYCODE));
        } else {
            info("vertex attribute '%s' is unsupported and will be ignored", name);
        }
    }

    /* make sure we have at least the required vertex attributes */
    if(!igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX,
            _topology_vertexAttributeToString(VERTEX_ATTR_ID))) {
        warning("the vertex attribute '%s' of type '%s' is required but not provided",
                _topology_vertexAttributeToString(VERTEX_ATTR_ID),
                _topology_igraphAttributeTypeToString(IGRAPH_ATTRIBUTE_STRING));
        isSuccess = FALSE;
    }
    if(!igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX,
            _topology_vertexAttributeToString(VERTEX_ATTR_BANDWIDTHDOWN))) {
        warning("the vertex attribute '%s' of type '%s' is required but not provided",
                _topology_vertexAttributeToString(VERTEX_ATTR_BANDWIDTHDOWN),
                _topology_igraphAttributeTypeToString(IGRAPH_ATTRIBUTE_NUMERIC));
        isSuccess = FALSE;
    }
    if(!igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX,
            _topology_vertexAttributeToString(VERTEX_ATTR_BANDWIDTHUP))) {
        warning("the vertex attribute '%s' of type '%s' is required but not provided",
                _topology_vertexAttributeToString(VERTEX_ATTR_BANDWIDTHUP),
                _topology_igraphAttributeTypeToString(IGRAPH_ATTRIBUTE_NUMERIC));
        isSuccess = FALSE;
    }

    /* check all provided edges attributes */
    for(i = 0; i < igraph_strvector_size(&enames); i++) {
        name = NULL;
        igraph_strvector_get(&enames, (glong) i, &name);
        type = igraph_vector_e(&etypes, (glong) i);

        debug("found edge attribute '%s' with type '%s'", name, _topology_igraphAttributeTypeToString(type));

        if(_topology_isValidEdgeAttributeKey(name, EDGE_ATTR_LATENCY)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_NUMERIC);
        } else if(_topology_isValidEdgeAttributeKey(name, EDGE_ATTR_JITTER)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_NUMERIC);
        } else if(_topology_isValidEdgeAttributeKey(name, EDGE_ATTR_PACKETLOSS)) {
            isSuccess = _topology_checkAttributeType(name, type, IGRAPH_ATTRIBUTE_NUMERIC);
        } else {
            info("edge attribute '%s' is unsupported and will be ignored", name);
        }
    }

    /* make sure we have at least the required edge attributes */
    if(!igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_EDGE,
            _topology_edgeAttributeToString(EDGE_ATTR_LATENCY))) {
        warning("the edge attribute '%s' of type '%s' is required but not provided",
                _topology_edgeAttributeToString(EDGE_ATTR_LATENCY),
                _topology_igraphAttributeTypeToString(IGRAPH_ATTRIBUTE_NUMERIC));
        isSuccess = FALSE;
    }
    if(!igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_EDGE,
            _topology_edgeAttributeToString(EDGE_ATTR_PACKETLOSS))) {
        warning("the edge attribute '%s' of type '%s' is required but not provided",
                _topology_edgeAttributeToString(EDGE_ATTR_PACKETLOSS),
                _topology_igraphAttributeTypeToString(IGRAPH_ATTRIBUTE_NUMERIC));
        isSuccess = FALSE;
    }

cleanup:
    igraph_strvector_destroy(&gnames);
    igraph_vector_destroy(&gtypes);
    igraph_strvector_destroy(&vnames);
    igraph_vector_destroy(&vtypes);
    igraph_strvector_destroy(&enames);
    igraph_vector_destroy(&etypes);

    if(isSuccess) {
        message("successfully verified all graph, vertex, and edge attributes");
    } else {
        warning("we could not properly validate all graph, vertex, and edge attributes");
    }

    return isSuccess;
}

static gboolean _topology_checkGraphProperties(Topology* top) {
    MAGIC_ASSERT(top);
    gint result = 0;

    message("checking graph properties...");

    if(!_topology_checkGraphAttributes(top)) {
        critical("topology validation failed because of problem with graph, vertex, or edge attributes");
        return FALSE;
    }

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

    top->isDirected = igraph_is_directed(&top->graph);

    gboolean is_complete;
    if (!_topology_isComplete(top, &is_complete)) {
        critical("Couldn't determine if topology is complete");
        return FALSE;
    }
    top->isComplete = (igraph_bool_t)is_complete;

    /* if the value is not set in the graph, we default to always using shortest path */
    gboolean prefersDirectPaths = FALSE;

    /* check if the graph sets a preference for using direct paths.
     * get the string value of the graph attribute, and check for true of false.
     * we must use a string because there is a bug in igraph boolean attribute code:
     * https://github.com/igraph/igraph/issues/1056 */
    const gchar* value;

    if (_topology_findGraphAttributeString(top, GRAPH_ATTR_PREFERDIRECTPATHS, &value)) {
        /* extract the boolean value from the string */
        igraph_bool_t valueIsTrue = 0;
        if(g_ascii_strncasecmp(value, "true", 4) == 0 ||
                g_ascii_strncasecmp(value, "yes", 3) == 0 ||
                g_ascii_strncasecmp(value, "1", 1) == 0) {
            valueIsTrue = 1;
        } else {
            valueIsTrue = 0;
        }

        /* check if it is true or not */
        if (valueIsTrue) {
            message("If a direct path between any pair of nodes exists, Shadow will prefer it over shortest path.");
            prefersDirectPaths = TRUE;
        } else {
            message("Shadow will always use shortest path between a pair of nodes, even if a direct path exists "
                    "(to override, set '%s' to 'yes' or 'true' or '1' to enable)",
                    _topology_graphAttributeToString(GRAPH_ATTR_PREFERDIRECTPATHS));
        }
    }
    top->prefersDirectPaths = prefersDirectPaths;

    message("topology graph is %s, %s, and %s with %u %s. It does%s prefer direct paths.",
            top->isComplete ? "complete" : "incomplete",
            top->isDirected ? "directed" : "undirected",
            top->isConnected ? "strongly connected" : "disconnected",
            (guint)top->clusterCount, top->clusterCount == 1 ? "cluster" : "clusters",
            top->prefersDirectPaths ? "" : " not");

    /* it must be connected so everyone can route to everyone else */
    if(!top->isConnected || top->clusterCount > 1) {
        critical("topology must be strongly connected with a single cluster; "
                "it is %sconnected with %i cluster%s",
                top->isConnected ? "" : "dis",
                (gint)top->clusterCount, top->clusterCount == 1 ? "" : "s");
        return FALSE;
    }

    return TRUE;
}

static gboolean _topology_checkGraphVerticesHelperHook(Topology* top, igraph_integer_t vertexIndex, gpointer userData) {
    MAGIC_ASSERT(top);

    /* the required attributes were already verified when we check the graph
     * properties. but that just means they are defined on the graph. the value
     * may still be NULL of invalid on each vertex.
     * in this func we get vertex attributes: S for string and N for numeric */
    gboolean isSuccess = TRUE;
    GString* message = g_string_new(NULL);
    g_string_printf(message, "found vertex %li", (glong)vertexIndex);

    /* keep a copy of the id once we get it to make the following message more understandable */
    gchar* idStr = NULL;

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* idKey = _topology_vertexAttributeToString(VERTEX_ATTR_ID);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, idKey)) {
        const gchar* vidStr;
        if(_topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_ID, &vidStr)) {
            g_string_append_printf(message, " %s='%s'", idKey, vidStr);
            idStr = g_strdup(vidStr);
        } else {
            warning("required attribute '%s' on vertex %li is NULL", idKey, (glong)vertexIndex);
            isSuccess = FALSE;
            idStr = g_strdup("NULL");
        }
    } else {
        warning("required attribute '%s' on vertex %li is missing", idKey, (glong)vertexIndex);
        isSuccess = FALSE;
        idStr = g_strdup("MISSING");
    }

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* bandwidthdownKey = _topology_vertexAttributeToString(VERTEX_ATTR_BANDWIDTHDOWN);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, bandwidthdownKey)) {
        gdouble bandwidthdownValue;
        if(_topology_findVertexAttributeDouble(top, vertexIndex, VERTEX_ATTR_BANDWIDTHDOWN, &bandwidthdownValue) &&
                bandwidthdownValue > 0.0f) {
            g_string_append_printf(message, " %s='%f'", bandwidthdownKey, bandwidthdownValue);
        } else {
            /* its an error if they gave a value that is incorrect */
            warning("required attribute '%s' on vertex %li (%s='%s') is NAN or negative",
                    bandwidthdownKey, (glong)vertexIndex, idKey, idStr);
            isSuccess = FALSE;
        }
    } else {
        warning("required attribute '%s' on vertex %li (%s='%s') is missing",
                bandwidthdownKey, (glong)vertexIndex, idKey, idStr);
        isSuccess = FALSE;
    }

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* bandwidthupKey = _topology_vertexAttributeToString(VERTEX_ATTR_BANDWIDTHUP);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, bandwidthupKey)) {
        gdouble bandwidthupValue;
        if(_topology_findVertexAttributeDouble(top, vertexIndex, VERTEX_ATTR_BANDWIDTHUP, &bandwidthupValue) &&
                bandwidthupValue > 0.0f) {
            g_string_append_printf(message, " %s='%f'", bandwidthupKey, bandwidthupValue);
        } else {
            /* its an error if they gave a value that is incorrect */
            warning("required attribute '%s' on vertex %li (%s='%s') is NAN or negative",
                    bandwidthupKey, (glong)vertexIndex, idKey, idStr);
            isSuccess = FALSE;
        }
    } else {
        warning("required attribute '%s' on vertex %li (%s='%s') is missing", bandwidthupKey, (glong)vertexIndex, idKey, idStr);
        isSuccess = FALSE;
    }

    /* this attribute is NOT required, so it is OK if it doesn't exist */
    const gchar* ipKey = _topology_vertexAttributeToString(VERTEX_ATTR_IP);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, ipKey)) {
        const gchar* ipVal;
        if(_topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_IP, &ipVal)) {
            g_string_append_printf(message, " %s='%s'", ipKey, ipVal);
        } else {
            info("optional attribute '%s' on vertex %li (%s='%s') is NULL, ignoring",
                    ipKey, (glong)vertexIndex, idKey, idStr);
        }
    }

    /* this attribute is NOT required, so it is OK if it doesn't exist */
    const gchar* citycodeKey = _topology_vertexAttributeToString(VERTEX_ATTR_CITYCODE);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, citycodeKey)) {
        const gchar* citycodeVal;
        if(_topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_CITYCODE, &citycodeVal)) {
            g_string_append_printf(message, " %s='%s'", citycodeKey, citycodeVal);
        } else {
            info("optional attribute '%s' on vertex %li (%s='%s') is NULL, ignoring",
                    citycodeKey, (glong)vertexIndex, idKey, idStr);
        }
    }

    /* this attribute is NOT required, so it is OK if it doesn't exist */
    const gchar* countrycodeKey = _topology_vertexAttributeToString(VERTEX_ATTR_COUNTRYCODE);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, countrycodeKey)) {
        const gchar* countrycodeVal;
        if(_topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_COUNTRYCODE, &countrycodeVal)) {
            g_string_append_printf(message, " %s='%s'", countrycodeKey, countrycodeVal);
        } else {
            info("optional attribute '%s' on vertex %li (%s='%s') is NULL, ignoring",
                    countrycodeKey, (glong)vertexIndex, idKey, idStr);
        }
    }

    /* this attribute is NOT required, so it is OK if it doesn't exist */
    const gchar* geocodeKey = _topology_vertexAttributeToString(VERTEX_ATTR_GEOCODE);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, geocodeKey)) {
        const gchar* geocodeVal;
        if(_topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_GEOCODE, &geocodeVal)) {
            g_string_append_printf(message, " %s='%s'", geocodeKey, geocodeVal);
        } else {
            info("optional attribute '%s' on vertex %li (%s='%s') is NULL, ignoring",
                    geocodeKey, (glong)vertexIndex, idKey, idStr);
        }
    }

    /* this attribute is NOT required, so it is OK if it doesn't exist */
    const gchar* typeKey = _topology_vertexAttributeToString(VERTEX_ATTR_TYPE);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, typeKey)) {
        const gchar* typeVal;
        if(_topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_TYPE, &typeVal)) {
            g_string_append_printf(message, " %s='%s'", typeKey, typeVal);
        } else {
            info("optional attribute '%s' on vertex %li (%s='%s') is NULL, ignoring",
                    typeKey, (glong)vertexIndex, idKey, idStr);
        }
    }

    /* this attribute is NOT required, so it is OK if it doesn't exist */
    const gchar* asnKey = _topology_vertexAttributeToString(VERTEX_ATTR_ASN);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, asnKey)) {
        gdouble asnValue;
        if(_topology_findVertexAttributeDouble(top, vertexIndex, VERTEX_ATTR_ASN, &asnValue)) {
            if(asnValue > 0.0f) {
                g_string_append_printf(message, " %s='%f'", asnKey, asnValue);
            } else {
                /* its an error if they gave a value that is incorrect */
                warning("optional attribute '%s' on vertex %li (%s='%s') is non-positive",
                        asnKey, (glong)vertexIndex, idKey, idStr);
                isSuccess = FALSE;
            }
        }
    }

    /* this attribute is NOT required, so it is OK if it doesn't exist */
    const gchar* packetlossKey = _topology_vertexAttributeToString(VERTEX_ATTR_PACKETLOSS);
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_VERTEX, packetlossKey)) {
        gdouble packetlossValue;
        if(_topology_findVertexAttributeDouble(top, vertexIndex, VERTEX_ATTR_PACKETLOSS, &packetlossValue)) {
            if(packetlossValue >= 0.0f && packetlossValue <= 1.0f) {
                g_string_append_printf(message, " %s='%f'", packetlossKey, packetlossValue);
            } else {
                /* its an error if they gave a value that is incorrect */
                warning("optional attribute '%s' on vertex %li (%s='%s') is out of range [0.0,1.0]",
                        packetlossKey, (glong)vertexIndex, idKey, idStr);
                isSuccess = FALSE;
            }
        }
    }

    debug("%s", message->str);

    g_string_free(message, TRUE);
    g_free(idStr);

    return isSuccess;
}

static igraph_integer_t _topology_iterateAllVertices(Topology* top, VertexNotifyFunc hook, gpointer userData) {
    MAGIC_ASSERT(top);
    utility_assert(hook);

    gboolean isSuccess = TRUE;

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
        if(!hook(top, (igraph_integer_t) vertexIndex, userData)) {
            isSuccess = FALSE;
        }

        vertexCount++;
        IGRAPH_VIT_NEXT(vertexIterator);
    }

    /* clean up */
    igraph_vit_destroy(&vertexIterator);

    if(isSuccess) {
        return vertexCount;
    } else {
        warning("we had a problem validating vertex attributes");
        return -1;
    }
}

static gboolean _topology_checkGraphVertices(Topology* top) {
    MAGIC_ASSERT(top);

    message("checking graph vertices...");

    igraph_integer_t vertexCount = _topology_iterateAllVertices(top, _topology_checkGraphVerticesHelperHook, NULL);
    if(vertexCount < 0) {
        /* there was some kind of error */
        warning("unable to validate graph vertices");
        return FALSE;
    }

    top->vertexCount = igraph_vcount(&top->graph);
    if(top->vertexCount != vertexCount) {
        warning("igraph_vcount %d does not match iterator count %d", top->vertexCount, vertexCount);
    }

    message("%u graph vertices ok", (guint) top->vertexCount);

    return TRUE;
}

static gboolean _topology_checkGraphEdgesHelperHook(Topology* top, igraph_integer_t edgeIndex, gpointer userData) {
    MAGIC_ASSERT(top);

    igraph_integer_t fromVertexIndex, toVertexIndex;
    gint result = igraph_edge(&top->graph, edgeIndex, &fromVertexIndex, &toVertexIndex);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_edge return non-success code %i", result);
        return FALSE;
    }

    gboolean found;
    const gchar* fromIDStr;
    const gchar* toIDStr;

    found = _topology_findVertexAttributeString(top, fromVertexIndex, VERTEX_ATTR_ID, &fromIDStr);
    utility_assert(found);
    found = _topology_findVertexAttributeString(top, toVertexIndex, VERTEX_ATTR_ID, &toIDStr);
    utility_assert(found);

    gboolean isSuccess = TRUE;

    GString* message = g_string_new(NULL);
    g_string_printf(message, "found edge %li", (glong)edgeIndex);

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* latencyKey = _topology_edgeAttributeToString(EDGE_ATTR_LATENCY);
    gdouble latencyValue;
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_EDGE, latencyKey) &&
            _topology_findEdgeAttributeDouble(top, edgeIndex, EDGE_ATTR_LATENCY, &latencyValue)) {
        if(latencyValue > 0.0f) {
            g_string_append_printf(message, " %s='%f'", latencyKey, latencyValue);
        } else {
            /* its an error if they gave a value that is incorrect */
            warning("required attribute '%s' on edge %li (from '%s' to '%s') is non-positive",
                    latencyKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    } else {
        warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing or NAN",
                latencyKey, (glong)edgeIndex, fromIDStr, toIDStr);
        isSuccess = FALSE;
    }

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* packetlossKey = _topology_edgeAttributeToString(EDGE_ATTR_PACKETLOSS);
    gdouble packetlossValue;
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_EDGE, packetlossKey) &&
        _topology_findEdgeAttributeDouble(top, edgeIndex, EDGE_ATTR_PACKETLOSS, &packetlossValue)) {

        if(packetlossValue >= 0.0f && packetlossValue <= 1.0f) {
            g_string_append_printf(message, " %s='%f'", packetlossKey, packetlossValue);
        } else {
            /* its an error if they gave a value that is incorrect */
            warning("required attribute '%s' on edge %li (from '%s' to '%s') is out of range [0.0,1.0]",
                    packetlossKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    } else {
        warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing or NAN",
                packetlossKey, (glong)edgeIndex, fromIDStr, toIDStr);
        isSuccess = FALSE;
    }

    /* this attribute is optional, so it is OK if it doesn't exist */
    const gchar* jitterKey = _topology_edgeAttributeToString(EDGE_ATTR_JITTER);
    gdouble jitterValue;
    if(igraph_cattribute_has_attr(&top->graph, IGRAPH_ATTRIBUTE_EDGE, jitterKey) &&
            _topology_findEdgeAttributeDouble(top, edgeIndex, EDGE_ATTR_JITTER, &jitterValue)) {
        if(jitterValue >= 0.0f) {
            g_string_append_printf(message, " %s='%f'", jitterKey, jitterValue);
        } else {
            /* its an error if they gave a value that is incorrect */
            warning("optional attribute '%s' on edge %li (from '%s' to '%s') is negative",
                    jitterKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    }

    debug("%s", message->str);

    g_string_free(message, TRUE);

    return isSuccess;
}

static igraph_integer_t _topology_iterateAllEdges(Topology* top, EdgeNotifyFunc hook, gpointer userData) {
    MAGIC_ASSERT(top);
    utility_assert(hook);

    gboolean isSuccess = TRUE;

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
        if(!hook(top, (igraph_integer_t) edgeIndex, userData)) {
            isSuccess = FALSE;
        }

        edgeCount++;
        IGRAPH_EIT_NEXT(edgeIterator);
    }

    igraph_eit_destroy(&edgeIterator);


    if(isSuccess) {
        return edgeCount;
    } else {
        warning("we had a problem validating edge attributes");
        return -1;
    }
}

static gboolean _topology_checkGraphEdges(Topology* top) {
    MAGIC_ASSERT(top);

    message("checking graph edges...");

    igraph_integer_t edgeCount = _topology_iterateAllEdges(top, _topology_checkGraphEdgesHelperHook, NULL);
    if(edgeCount < 0) {
        /* there was some kind of error */
        warning("unable to validate graph edges");
        return FALSE;
    }

    top->edgeCount = igraph_ecount(&top->graph);
    if(top->edgeCount != edgeCount) {
        warning("igraph_vcount %d does not match iterator count %d", top->edgeCount, edgeCount);
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
    const gchar* latencyKey = _topology_edgeAttributeToString(EDGE_ATTR_LATENCY);
    result = EANV(&top->graph, latencyKey, top->edgeWeights);
    g_rw_lock_writer_unlock(&(top->edgeWeightsLock));
    _topology_unlockGraph(top);
    if(result != IGRAPH_SUCCESS) {
        critical("igraph_cattribute_EANV return non-success code %i", result);
        return FALSE;
    }

    return TRUE;
}

static gboolean _topology_verticesAreAdjacent(Topology* top, igraph_integer_t srcVertexIndex, igraph_integer_t dstVertexIndex) {
    MAGIC_ASSERT(top);

    igraph_integer_t edge_id = -1;
    gint result;

    _topology_lockGraph(top);
    result = _topology_getEdgeHelper(top, srcVertexIndex, dstVertexIndex, &edge_id, NULL, NULL);
    _topology_unlockGraph(top);

    if (result != IGRAPH_SUCCESS) {
        warning("Unable to determine whether or not an edge exists between vertexes %d and %d",
                srcVertexIndex, dstVertexIndex);
        return FALSE;
    }
    return edge_id >= 0;
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
#ifdef USE_PERF_TIMERS
    message("path cache cleared, spent %f seconds computing %u shortest paths with dijkstra, "
            "and %f seconds computing %u shortest self paths",
            top->shortestPathTotalTime, top->shortestPathCount,
            top->selfPathTotalTime, top->selfPathCount);
#else
    message("path cache cleared, computed %u shortest paths with dijkstra, "
            "and %u shortest self paths",
            top->shortestPathCount, top->selfPathCount);
#endif
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

static gboolean _topology_shouldStorePath(Topology* top, gboolean isDirectPath,
        igraph_integer_t srcVertexIndex, igraph_integer_t dstVertexIndex) {
    MAGIC_ASSERT(top);

    /* double check that we don't overwrite existing path entries */
    Path* srcToDstCachedPath = _topology_getPathFromCache(top, srcVertexIndex, dstVertexIndex);
    Path* dstToSrcCachedPath = _topology_getPathFromCache(top, dstVertexIndex, srcVertexIndex);

    if(srcToDstCachedPath != NULL || dstToSrcCachedPath != NULL) {
        /* we already have a cached path entry in one direction or the other */
        return FALSE;
    }

    /* sanity check: complete graphs should only have direct paths and nothing else */
    if(top->isComplete && !isDirectPath) {
        return FALSE;
    }

    if(top->prefersDirectPaths && !isDirectPath) {
        /* we only accept a non-direct path if a direct path does not exist in the graph */
        gboolean verticesAreAdjacent = _topology_verticesAreAdjacent(top, srcVertexIndex, dstVertexIndex);
        if(verticesAreAdjacent) {
            /* the path we are trying to store is not a direct path, but the graph does have one */
            return FALSE;
        }
    }

    /* ok to store the path */
    return TRUE;
}

static void _topology_storePathInCache(Topology* top, gboolean isDirectPath,
        igraph_integer_t srcVertexIndex, igraph_integer_t dstVertexIndex,
        igraph_real_t totalLatency, igraph_real_t totalReliability) {
    MAGIC_ASSERT(top);

    /* make sure we don't store a non-direct path if we want a direct one and it exists */
    if(!_topology_shouldStorePath(top, isDirectPath, srcVertexIndex, dstVertexIndex)) {
        return;
    }

    gdouble latencyMS = (gdouble) totalLatency;
    gdouble reliability = (gdouble) totalReliability;
    gboolean wasUpdated = FALSE;

    g_rw_lock_writer_lock(&(top->pathCacheLock));

    /* create latency cache on the fly */
    if(!top->pathCache) {
        /* stores hash tables for source address caches */
        top->pathCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_hash_table_destroy);
    }

    GHashTable* srcCache = g_hash_table_lookup(top->pathCache, GINT_TO_POINTER(srcVertexIndex));
    if(!srcCache) {
        /* dont have a cache for this source yet, create one now */
        srcCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)path_free);
        g_hash_table_replace(top->pathCache, GINT_TO_POINTER(srcVertexIndex), srcCache);
    }

    /* create the path */
    Path* path = path_new(isDirectPath, (gint64)srcVertexIndex, (gint64)dstVertexIndex, latencyMS, reliability);

    /* store it in the cache. don't bother storing the path for the reverse direction,
     * because we can check both directions for this cached path later. */
    g_hash_table_replace(srcCache, GINT_TO_POINTER(dstVertexIndex), path);

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

static gboolean _topology_computePathProperties(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_vector_t* resultPathVertices, GString* pathStringBuffer,
        igraph_real_t* pathLatencyOut, igraph_real_t* pathReliabilityOut, igraph_integer_t* pathTargetIndexOut) {
    MAGIC_ASSERT(top);

    /* WARNING This function should only be called when there are more than 0 result paths, ie,
     * when the src and dst are not attached to the same vertex.
     *
     * each position represents a single destination.
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

    igraph_integer_t targetVertexIndex = (igraph_integer_t) -1;
    const gchar* dstIDStr = NULL;
    const gchar* srcIDStr = NULL;

    glong nVertices = igraph_vector_size(resultPathVertices);
    utility_assert(nVertices > 0);

    _topology_lockGraph(top);

    /* get source properties */
    gdouble sourcePacketLoss;
    if(_topology_findVertexAttributeDouble(top, srcVertexIndex, VERTEX_ATTR_PACKETLOSS, &sourcePacketLoss)) {
        totalReliability *= (1.0f - sourcePacketLoss);
    }

    gboolean found = _topology_findVertexAttributeString(top, srcVertexIndex, VERTEX_ATTR_ID, &srcIDStr);
    utility_assert(found);
    g_string_printf(pathStringBuffer, "%s", srcIDStr);

    /* get destination properties */
    targetVertexIndex = (igraph_integer_t) igraph_vector_tail(resultPathVertices);
    found = _topology_findVertexAttributeString(top, targetVertexIndex, VERTEX_ATTR_ID, &dstIDStr);
    utility_assert(found);

    /* only include dst loss if there is no path between src and dst vertices */
    if((srcVertexIndex != targetVertexIndex) || (srcVertexIndex == targetVertexIndex && nVertices > 2)) {
        gdouble destPacketLoss;
        if(_topology_findVertexAttributeDouble(top, targetVertexIndex, VERTEX_ATTR_PACKETLOSS, &destPacketLoss)) {
            totalReliability *= (1.0f - destPacketLoss);
        }
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

        const gchar* toIDStr;
        gboolean found = _topology_findVertexAttributeString(top, toVertexIndex, VERTEX_ATTR_ID, &toIDStr);
        utility_assert(found);

        igraph_real_t edgeLatency = 0, edgeReliability = 0;
        igraph_integer_t edgeIndex = 0;

        result = _topology_getEdgeHelper(top, fromVertexIndex, toVertexIndex, &edgeIndex, &edgeLatency, &edgeReliability);

        if(result != IGRAPH_SUCCESS || edgeIndex < 0) {
            _topology_unlockGraph(top);
            critical("igraph_get_eid return non-success code %i for edge between "
                     "%s (%i) and %s (%i)", result, fromIDStr, (gint) fromVertexIndex, toIDStr, (gint) toVertexIndex);
            return FALSE;
        }

        /* accumulate path attributes */
        totalLatency += edgeLatency;
        totalReliability *= edgeReliability;

        /* accumulate path information */
        g_string_append_printf(pathStringBuffer, "%s[%f,%f]-->%s",
                top->isDirected ? "--" : "<--", edgeLatency, 1.0f-edgeReliability, toIDStr);

        /* update for next edge */
        fromVertexIndex = toVertexIndex;
        fromIDStr = toIDStr;
    }

    _topology_unlockGraph(top);

    if(pathLatencyOut) {
        *pathLatencyOut = totalLatency;
    }
    if(pathReliabilityOut) {
        *pathReliabilityOut = totalReliability;
    }
    if(pathTargetIndexOut) {
        *pathTargetIndexOut = targetVertexIndex;
    }

    return TRUE;
}

static GQueue* _topology_getUniqueVertexTargets(Topology* top) {
    MAGIC_ASSERT(top);

    GQueue* uniqueVertexIDs = g_queue_new();

    GHashTableIter iter;
    gpointer key, value;

    g_rw_lock_reader_lock(&(top->virtualIPLock));

    g_hash_table_iter_init(&iter, top->verticesWithAttachedHosts);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        g_queue_push_tail(uniqueVertexIDs, value);
    }

    g_rw_lock_reader_unlock(&(top->virtualIPLock));

    return uniqueVertexIDs;
}

static gboolean _topology_computeShortestPathToSelf(Topology* top, igraph_integer_t vertexIndex, const gchar* idStr) {
    MAGIC_ASSERT(top);

    igraph_real_t minLatency = 0.0f;
    igraph_real_t reliabilityOfMinLatencyEdge = 0.0f;
    igraph_integer_t indexOfMinLatencyEdge = 0;
    gint result = 0;
    gboolean found = FALSE;

    /* iterate over all outgoing edges from vertex, get the shortest, and use it twice */
    _topology_lockGraph(top);

    /* first we 'select' the incident edges, that is, those to which vertexIndex is connected */
    igraph_es_t edgeSelector;
    result = igraph_es_incident(&edgeSelector, vertexIndex, IGRAPH_OUT);

    if(result != IGRAPH_SUCCESS) {
        critical("igraph_es_incident return non-success code %i", result);
        _topology_unlockGraph(top);
        return FALSE;
    }

    /* now we set up an iterator on these edges */
    igraph_eit_t edgeIterator;
    result = igraph_eit_create(&top->graph, edgeSelector, &edgeIterator);

    if(result != IGRAPH_SUCCESS) {
        critical("igraph_eit_create return non-success code %i", result);
        igraph_es_destroy(&edgeSelector);
        _topology_unlockGraph(top);
        return FALSE;
    }

#ifdef USE_PERF_TIMERS
    /* time the shortest path loop */
    GTimer* pathTimer = g_timer_new();
#endif

    /* keep the min latency and packetloss while iterating */
    while (!IGRAPH_EIT_END(edgeIterator)) {
        igraph_integer_t edgeIndex = IGRAPH_EIT_GET(edgeIterator);

        igraph_real_t edgeLatency = 0.0f;
        igraph_real_t edgePacketLoss = 0.0f;

        /* latency and packet loss are required attributes on edges */
        found = _topology_findEdgeAttributeDouble(top, edgeIndex, EDGE_ATTR_LATENCY, &edgeLatency);
        utility_assert(found);

        if(minLatency == 0 || edgeLatency < minLatency) {
            minLatency = edgeLatency;

            found = _topology_findEdgeAttributeDouble(top, edgeIndex, EDGE_ATTR_PACKETLOSS, &edgePacketLoss);
            utility_assert(found);
            reliabilityOfMinLatencyEdge = 1.0f - edgePacketLoss;

            indexOfMinLatencyEdge = edgeIndex;
        }

        IGRAPH_EIT_NEXT(edgeIterator);
    }

    _topology_unlockGraph(top);

#ifdef USE_PERF_TIMERS
    /* track the time spent running the algorithm */
    gdouble elapsedSeconds = g_timer_elapsed(pathTimer, NULL);
    g_timer_destroy(pathTimer);
#endif

    igraph_eit_destroy(&edgeIterator);
    igraph_es_destroy(&edgeSelector);

    g_mutex_lock(&top->topologyLock);
#ifdef USE_PERF_TIMERS
    top->selfPathTotalTime += elapsedSeconds;
#endif
    top->selfPathCount++;
    g_mutex_unlock(&top->topologyLock);

    _topology_lockGraph(top);

    /* get the other vertex that we chose */
    igraph_integer_t fromVertexIndex, toVertexIndex;
    result = igraph_edge(&top->graph, indexOfMinLatencyEdge, &fromVertexIndex, &toVertexIndex);

    if(result != IGRAPH_SUCCESS) {
        critical("igraph_edge return non-success code %i", result);
        _topology_unlockGraph(top);
        return FALSE;
    }

    /* figure out which index is the other side of the edge */
    igraph_integer_t targetIndex = (fromVertexIndex == vertexIndex) ? toVertexIndex : fromVertexIndex;
    const gchar* targetIDStr;
    found = _topology_findVertexAttributeString(top, targetIndex, VERTEX_ATTR_ID, &targetIDStr);
    utility_assert(found);

    _topology_unlockGraph(top);

    /* this edge will be used "twice" to get back to source */
    igraph_real_t latency = 2.0f * minLatency;
    igraph_real_t reliability = reliabilityOfMinLatencyEdge * reliabilityOfMinLatencyEdge;

    info("shortest path back to self is %f ms with %f loss, path: "
            "%s%s--[%f,%f]-->%s%s--[%f,%f]-->%s",
            (gdouble)latency, (gdouble)(1.0f-reliability),
            idStr, top->isDirected ? "" : "<", minLatency, 1.0f-reliabilityOfMinLatencyEdge,
            targetIDStr, top->isDirected ? "" : "<", minLatency, 1.0f-reliabilityOfMinLatencyEdge, idStr);

    /* cache the latency and reliability we just computed */
    _topology_storePathInCache(top, FALSE, vertexIndex, vertexIndex, latency, reliability);

    return TRUE;
}

static gboolean _topology_computeSourcePaths(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_integer_t dstVertexIndex) {
    MAGIC_ASSERT(top);
    utility_assert(srcVertexIndex >= 0);
    utility_assert(dstVertexIndex >= 0);

    gboolean found;
    _topology_lockGraph(top);
    const gchar* srcIDStr;
    found = _topology_findVertexAttributeString(top, srcVertexIndex, VERTEX_ATTR_ID, &srcIDStr);
    utility_assert(found);
    const gchar* dstIDStr;
    found = _topology_findVertexAttributeString(top, dstVertexIndex, VERTEX_ATTR_ID, &dstIDStr);
    utility_assert(found);
    _topology_unlockGraph(top);

    info("requested path between source vertex %li (%s) and destination vertex %li (%s)",
            (glong)srcVertexIndex, srcIDStr, (glong)dstVertexIndex, dstIDStr);

    if(srcVertexIndex == dstVertexIndex) {
        return _topology_computeShortestPathToSelf(top, srcVertexIndex, srcIDStr);
    }

    /* we are going to compute shortest path from the source to all attached destinations
     * (including dstAddress) in order to cut down on the the number of dijkstra runs we do.
     * but we only need to do it once for each unique vertex no matter how many hosts live there. */
    GQueue* attachedTargets = _topology_getUniqueVertexTargets(top);

    /* normally we should hold the lock while modifying the list, but since the virtualIPLock
     * hash table stores vertex indices in pointers, this should be OK. */
    guint numTargets = g_queue_get_length(attachedTargets);

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
    for(gint position = 0; position < numTargets; position++) {
        gpointer vertexIndexPointer = g_queue_pop_head(attachedTargets);
        utility_assert(vertexIndexPointer != NULL);

        /* set each vertex index as a destination for dijkstra */
        igraph_integer_t vertexIndex = (igraph_integer_t) GPOINTER_TO_INT(vertexIndexPointer);
        igraph_vector_set(&dstVertexIndexSet, position, (igraph_real_t) vertexIndex);
        utility_assert(vertexIndex == igraph_vector_e(&dstVertexIndexSet, position));

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

    if(attachedTargets) {
        g_queue_free(attachedTargets);
        attachedTargets = NULL;
    }

    utility_assert(numTargets == igraph_vector_size(&dstVertexIndexSet));
    utility_assert(numTargets == igraph_vector_ptr_size(&resultPaths));
    utility_assert(foundDstVertexIndex == TRUE);

    info("computing shortest paths from source vertex %li (%s) to all %u vertices with connected hosts",
            (glong)srcVertexIndex, srcIDStr, numTargets);

    _topology_lockGraph(top);
    g_rw_lock_reader_lock(&(top->edgeWeightsLock));

#ifdef USE_PERF_TIMERS
    /* time the dijkstra algorithm */
    GTimer* pathTimer = g_timer_new();
#endif

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

#ifdef USE_PERF_TIMERS
    /* track the time spent running the algorithm */
    gdouble elapsedSeconds = g_timer_elapsed(pathTimer, NULL);
#endif

    g_rw_lock_reader_unlock(&(top->edgeWeightsLock));
    _topology_unlockGraph(top);

#ifdef USE_PERF_TIMERS
    g_timer_destroy(pathTimer);
#endif

    g_mutex_lock(&top->topologyLock);
#ifdef USE_PERF_TIMERS
    top->shortestPathTotalTime += elapsedSeconds;
#endif
    top->shortestPathCount++;
    g_mutex_unlock(&top->topologyLock);

    if(result != IGRAPH_SUCCESS) {
        critical("igraph_get_shortest_paths_dijkstra return non-success code %i", result);
        return FALSE;
    }

    /* sanity checks */
    utility_assert(numTargets == igraph_vector_size(&dstVertexIndexSet));
    utility_assert(numTargets == igraph_vector_ptr_size(&resultPaths));

    /* process the results */
    gboolean isAllSuccess = TRUE;
    gboolean foundDstPosition = FALSE;
    GString* pathStringBuffer = g_string_new(NULL);

    /* go through the result paths for all targets */
    for(gint position = 0; position < numTargets; position++) {
        /* handle the path to the destination at this position */
        igraph_vector_t* resultPathVertices = igraph_vector_ptr_e(&resultPaths, position);

        /* check the number of vertices in the result path */
        glong nVertices = igraph_vector_size(resultPathVertices);

        /* if there are no vertices, then the source and destination hosts are attached to
         * the same igraph vertex. igraph doesn't give us a shortest path in this case.
         * so, we handle the other cases here where it does provide paths. */
        if(nVertices > 0) {
            igraph_integer_t pathTargetIndex = 0;
            igraph_real_t pathLatency = 0.0f, pathReliability = 0.0f;

            gboolean isSuccess = _topology_computePathProperties(top, srcVertexIndex, resultPathVertices,
                    pathStringBuffer, &pathLatency, &pathReliability, &pathTargetIndex);

            if(isSuccess) {
                const gchar* targetIDStr;
                _topology_lockGraph(top);
                found = _topology_findVertexAttributeString(top, pathTargetIndex, VERTEX_ATTR_ID, &targetIDStr);
                utility_assert(found);
                _topology_unlockGraph(top);

                GString* logMessage = g_string_new(NULL);

                g_string_printf(logMessage, "shortest path %s%s%s (%i%s%i) is %f ms with %f loss, path: %s",
                                    srcIDStr, top->isDirected ? "-->" : "<-->", targetIDStr,
                                    (gint) srcVertexIndex, top->isDirected ? "-->" : "<-->", (gint) pathTargetIndex,
                                    pathLatency, 1-pathReliability, pathStringBuffer->str);

                /* make sure at least one of the targets is the destination.
                 * the case where src and dest are the same are handled in _topology_computePathToSelf */
                if(dstVertexIndexPosition == position) {
                    utility_assert(dstVertexIndex == pathTargetIndex);
                    foundDstPosition = TRUE;
                    info("%s", logMessage->str);
                } else {
                    debug("%s", logMessage->str);
                }

                g_string_free(logMessage, TRUE);

                if(pathLatency == 0) {
                    warning("found shortest path latency of 0 ms between source %s (%i) and destination %s (%i), using 1 ms instead",
                            srcIDStr, srcVertexIndex, targetIDStr, pathTargetIndex);
                    pathLatency = 1;
                }

                /* cache the latency and reliability we just computed */
                _topology_storePathInCache(top, FALSE, srcVertexIndex, pathTargetIndex, pathLatency, pathReliability);
            } else {
                isAllSuccess = FALSE;
            }
        }

        /* we are now done with the resultPathVertices vector, clean up */
        igraph_vector_destroy(resultPathVertices);
        g_free(resultPathVertices);
    }

    utility_assert(foundDstPosition == TRUE);

    /* clean up */
    igraph_vector_ptr_destroy(&resultPaths);
    igraph_vector_destroy(&dstVertexIndexSet);
    g_string_free(pathStringBuffer, TRUE);

    /* success */
    return isAllSuccess;
}

static gboolean _topology_lookupDirectPath(Topology* top, igraph_integer_t srcVertexIndex,
        igraph_integer_t dstVertexIndex) {
    MAGIC_ASSERT(top);

    /* for complete graphs, we lookup the edge and use it as the path instead
     * of running the shortest path algorithm.
     *
     * see the comment in _topology_computeSourcePathsHelper
     */

    igraph_real_t totalLatency = 0.0, totalReliability = 1.0;
    igraph_real_t edgeLatency = 0.0, edgeReliability = 1.0;
    gboolean found;
    const gchar* srcIDStr;
    const gchar* dstIDStr;

    _topology_lockGraph(top);

    found = _topology_findVertexAttributeString(top, srcVertexIndex, VERTEX_ATTR_ID, &srcIDStr);
    utility_assert(found);
    found = _topology_findVertexAttributeString(top, dstVertexIndex, VERTEX_ATTR_ID, &dstIDStr);
    utility_assert(found);

    gdouble sourcePacketLoss;
    if(_topology_findVertexAttributeDouble(top, srcVertexIndex, VERTEX_ATTR_PACKETLOSS, &sourcePacketLoss)) {
        totalReliability *= (1.0f - sourcePacketLoss);
    }
    gdouble destPacketLoss;
    if(_topology_findVertexAttributeDouble(top, dstVertexIndex, VERTEX_ATTR_PACKETLOSS, &destPacketLoss)) {
        totalReliability *= (1.0f - destPacketLoss);
    }

    gint result = _topology_getEdgeHelper(top, srcVertexIndex, dstVertexIndex, NULL, &edgeLatency, &edgeReliability);

    if(result != IGRAPH_SUCCESS) {
        critical("igraph_get_eid return non-success code %i for edge between "
                 "%s (%i) and %s (%i)", result, srcIDStr, (gint) srcVertexIndex, dstIDStr, (gint) dstVertexIndex);
        _topology_unlockGraph(top);
        return FALSE;
    }

    _topology_unlockGraph(top);

    totalLatency += edgeLatency;
    totalReliability *= edgeReliability;

    /* cache the latency and reliability we just computed */
    _topology_storePathInCache(top, TRUE, srcVertexIndex, dstVertexIndex, totalLatency, totalReliability);

    return TRUE;
}

static void _topology_logAllCachedPathsHelper2(gpointer dstIndexKey, Path* path, Topology* top) {
    if(path) {

        gchar* pathStr = path_toString(path);

        igraph_integer_t srcVertexIndex = (igraph_integer_t)path_getSrcVertexIndex(path);
        igraph_integer_t dstVertexIndex = (igraph_integer_t)path_getDstVertexIndex(path);

        gboolean found;
        const gchar* srcIDStr;
        const gchar* dstIDStr;

        _topology_lockGraph(top);
        found = _topology_findVertexAttributeString(top, srcVertexIndex, VERTEX_ATTR_ID, &srcIDStr);
        utility_assert(found);
        found = _topology_findVertexAttributeString(top, dstVertexIndex, VERTEX_ATTR_ID, &dstIDStr);
        utility_assert(found);
        _topology_unlockGraph(top);

        /* log this at info level so we don't spam the message level logs */
        info("Found path %s%s%s in cache: %s",
                srcIDStr, top->isDirected ? "->" : "<->", dstIDStr, pathStr);

        g_free(pathStr);
    }
}

static void _topology_logAllCachedPathsHelper1(gpointer srcIndexKey, GHashTable* sourceCache, Topology* top) {
    if(sourceCache) {
        g_hash_table_foreach(sourceCache, (GHFunc)_topology_logAllCachedPathsHelper2, top);
    }
}

static void _topology_logAllCachedPaths(Topology* top) {
    MAGIC_ASSERT(top);
    if(top->pathCache) {
        g_hash_table_foreach(top->pathCache, (GHFunc)_topology_logAllCachedPathsHelper1, top);
    }
}

static Path* _topology_getPathEntry(Topology* top, Address* srcAddress, Address* dstAddress) {
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

        gboolean found;
        const gchar* srcIDStr;
        const gchar* dstIDStr;


        _topology_lockGraph(top);
        found = _topology_findVertexAttributeString(top, srcVertexIndex, VERTEX_ATTR_ID, &srcIDStr);
        utility_assert(found);
        found = _topology_findVertexAttributeString(top, dstVertexIndex, VERTEX_ATTR_ID, &dstIDStr);
        utility_assert(found);
        _topology_unlockGraph(top);

        gboolean verticesAreAdjacent = _topology_verticesAreAdjacent(top, srcVertexIndex, dstVertexIndex);

        info("We need a path between node %s at %s (vertex %i) and "
                "node %s at %s (vertex %i), topology properties are: "
                "isComplete=%s, prefersDirectPaths=%s, verticesAreAdjacent=%s",
                address_toString(srcAddress), srcIDStr, (gint)srcVertexIndex,
                address_toString(dstAddress), dstIDStr, (gint)dstVertexIndex,
                top->isComplete ? "True" : "False",
                top->prefersDirectPaths ? "True" : "False",
                verticesAreAdjacent ? "True" : "False");

        if(top->isComplete || (top->prefersDirectPaths && verticesAreAdjacent)) {
            /* use the edge between src and dst as the path */
            success = _topology_lookupDirectPath(top, srcVertexIndex, dstVertexIndex);

            if(success) {
                info("We found a direct path between node %s at %s (vertex %i) and "
                        "node %s at %s (vertex %i), and stored the path in the cache.",
                        address_toString(srcAddress), srcIDStr, (gint)srcVertexIndex,
                        address_toString(dstAddress), dstIDStr, (gint)dstVertexIndex);
            }
        } else {
            success = _topology_computeSourcePaths(top, srcVertexIndex, dstVertexIndex);
        }

        if(success) {
            path = _topology_getPathFromCache(top, srcVertexIndex, dstVertexIndex);
            if(!path) {
                path = _topology_getPathFromCache(top, dstVertexIndex, srcVertexIndex);
            }
        }

        if(!path) {
            /* some error finding the path */
            error("unable to find path between node %s at %s (vertex %i) "
                    "and node %s at %s (vertex %i)",
                    address_toString(srcAddress), srcIDStr, (gint)srcVertexIndex,
                    address_toString(dstAddress), dstIDStr, (gint)dstVertexIndex);
        }
    }


    return path;
}

void topology_incrementPathPacketCounter(Topology* top, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(top);

    Path* path = _topology_getPathEntry(top, srcAddress, dstAddress);
    if(path != NULL) {
        path_incrementPacketCount(path);
    } else {
        error("unable to find path between node %s and node %s",
                address_toString(srcAddress), address_toString(dstAddress));
    }
}

gdouble topology_getLatency(Topology* top, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(top);

    Path* path = _topology_getPathEntry(top, srcAddress, dstAddress);

    if(path != NULL) {
        return path_getLatency(path);
    } else {
        return (gdouble) -1;
    }
}

gdouble topology_getReliability(Topology* top, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(top);

    Path* path = _topology_getPathEntry(top, srcAddress, dstAddress);

    if(path != NULL) {
        return path_getReliability(path);
    } else {
        return (gdouble) -1;
    }
}

gboolean topology_isRoutable(Topology* top, Address* srcAddress, Address* dstAddress) {
    MAGIC_ASSERT(top);
    return (topology_getLatency(top, srcAddress, dstAddress) > -1) ? TRUE : FALSE;
}

static gboolean _topology_findAttachmentVertexHelperHook(Topology* top, igraph_integer_t vertexIndex, AttachHelper* ah) {
    MAGIC_ASSERT(top);
    utility_assert(ah);

    /* @warning: make sure we hold the graph lock when iterating with this helper
     * @todo: this could be made much more efficient */

    const gchar* idStr;
    gboolean idFound = _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_ID, &idStr);
    utility_assert(idFound);

    const gchar* ipStr = NULL;
    const gchar* citycodeStr = NULL;
    const gchar* countrycodeStr = NULL;
    const gchar* geocodeStr = NULL;
    const gchar* typeStr = NULL;

    gboolean ipFound = _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_IP, &ipStr);
    gboolean citycodeFound = _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_CITYCODE, &citycodeStr);
    gboolean countrycodeFound = _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_COUNTRYCODE, &countrycodeStr);
    gboolean geocodeFound = _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_GEOCODE, &geocodeStr);
    gboolean typeFound = _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_TYPE, &typeStr);

    gboolean citycodeMatches = citycodeFound && ah->citycodeHint && !g_ascii_strcasecmp(citycodeStr, ah->citycodeHint);
    gboolean countrycodeMatches = countrycodeFound && ah->countrycodeHint && !g_ascii_strcasecmp(countrycodeStr, ah->countrycodeHint);
    gboolean geocodeMatches = geocodeFound && ah->geocodeHint && !g_ascii_strcasecmp(geocodeStr, ah->geocodeHint);
    gboolean typeMatches = typeFound && ah->typeHint && !g_ascii_strcasecmp(typeStr, ah->typeHint);

    /* get the ip address of the vertex if there is one */
    gboolean vertexHasUsableIP = FALSE;
    in_addr_t vertexIP = INADDR_NONE;
    if(ipFound) {
        in_addr_t ip = address_stringToIP(ipStr);
        if(ip != INADDR_NONE && ip != INADDR_ANY && ip != INADDR_LOOPBACK) {
            vertexHasUsableIP = TRUE;
            vertexIP = ip;
        }
    }

    /* check for exact IP address match */
    if(ah->requestedIPIsUsable && vertexHasUsableIP) {
        if(vertexIP == ah->requestedIP) {
            if(!ah->foundExactIPMatch) {
                /* first time we found a match, clear all queues to make sure we only
                 * select from the matching IP vertices */
                g_queue_clear(ah->candidatesCityAndType);
                g_queue_clear(ah->candidatesCity);
                g_queue_clear(ah->candidatesCountryAndType);
                g_queue_clear(ah->candidatesCountry);
                g_queue_clear(ah->candidatesGeoAndType);
                g_queue_clear(ah->candidatesGeo);
                g_queue_clear(ah->candidatesAll);
                g_queue_clear(ah->candidatesType);
            }
            ah->foundExactIPMatch = TRUE;
            g_queue_push_tail(ah->candidatesAll, GINT_TO_POINTER(vertexIndex));
            if(vertexHasUsableIP) {
                ah->numIPsAll++;
            }
        }
    }

    /* if it matches the requested IP exactly, we ignore other the filters */
    if(ah->foundExactIPMatch) {
        return TRUE;
    }

    g_queue_push_tail(ah->candidatesAll, GINT_TO_POINTER(vertexIndex));
    if(vertexHasUsableIP) {
        ah->numIPsAll++;
    }

    if(citycodeMatches && typeMatches && ah->candidatesCityAndType) {
        g_queue_push_tail(ah->candidatesCityAndType, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numIPsCityAndType++;
        }
    }

    if(citycodeMatches && ah->candidatesCity) {
        g_queue_push_tail(ah->candidatesCity, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numIPsCity++;
        }
    }

    if(countrycodeMatches && typeMatches && ah->candidatesCountryAndType) {
        g_queue_push_tail(ah->candidatesCountryAndType, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numIPsCountryAndType++;
        }
    }

    if(countrycodeMatches && ah->candidatesCountry) {
        g_queue_push_tail(ah->candidatesCountry, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numIPsCountry++;
        }
    }

    if(geocodeMatches && typeMatches && ah->candidatesGeoAndType) {
        g_queue_push_tail(ah->candidatesGeoAndType, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numIPsGeoAndType++;
        }
    }

    if(geocodeMatches && ah->candidatesGeo) {
        g_queue_push_tail(ah->candidatesGeo, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numIPsGeo++;
        }
    }


    if(typeMatches && ah->candidatesType) {
        g_queue_push_tail(ah->candidatesType, GINT_TO_POINTER(vertexIndex));
        if(vertexHasUsableIP) {
            ah->numIPsType++;
        }
    }

    return TRUE;
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

        in_addr_t match = ~(vertexIP ^ ip);
        if(match > bestMatch || bestMatch == 0) {
            bestMatch = match;
            bestIP = vertexIP;
            bestVertexIndexPtr = vertexIndexPtr;
        }
    }

    return bestVertexIndexPtr;
}

static igraph_integer_t _topology_findAttachmentVertex(Topology* top, Random* randomSourcePool, in_addr_t nodeIP,
        gchar* ipHint, gchar* citycodeHint, gchar* countrycodeHint, gchar* geocodeHint, gchar* typeHint) {
    MAGIC_ASSERT(top);

    igraph_integer_t vertexIndex = (igraph_integer_t) -1;
    igraph_integer_t* vertexIndexPtr = GINT_TO_POINTER(-1);

    AttachHelper* ah = g_new0(AttachHelper, 1);
    ah->ipHint = ipHint;
    ah->citycodeHint = citycodeHint;
    ah->countrycodeHint = countrycodeHint;
    ah->geocodeHint = geocodeHint;
    ah->typeHint = typeHint;

    if(ipHint) {
        in_addr_t ip = address_stringToIP(ipHint);
        if(ip != INADDR_NONE && ip != INADDR_ANY && ip != INADDR_LOOPBACK) {
            ah->requestedIPIsUsable = TRUE;
            ah->requestedIP = ip;
        }
    }

    ah->candidatesCityAndType = g_queue_new();
    ah->candidatesCity = g_queue_new();
    ah->candidatesCountryAndType = g_queue_new();
    ah->candidatesCountry = g_queue_new();
    ah->candidatesGeoAndType = g_queue_new();
    ah->candidatesGeo = g_queue_new();
    ah->candidatesType = g_queue_new();
    ah->candidatesAll = g_queue_new();

    /* go through the vertices to see which ones match our hint filters */
    _topology_lockGraph(top);
    _topology_iterateAllVertices(top, (VertexNotifyFunc) _topology_findAttachmentVertexHelperHook, ah);
    _topology_unlockGraph(top);

    /* the logic here is to try and find the most specific match following the hints.
     * we always use exact IP hint matches, and otherwise use it to select the best possible
     * match from the final set of candidates. the type and code hints are used to filter
     * all vertices down to a smaller set. if that smaller set is empty, then we fall back to the
     * type-only filtered set and eventually the complete vertex set.
     */
    GQueue* candidates = NULL;
    gboolean useLongestPrefixMatching = FALSE;

    if(g_queue_get_length(ah->candidatesCityAndType) > 0) {
        candidates = ah->candidatesCityAndType;
        useLongestPrefixMatching = (ah->requestedIPIsUsable && ah->numIPsCityAndType > 0);
    } else if(g_queue_get_length(ah->candidatesCity) > 0) {
        candidates = ah->candidatesCity;
        useLongestPrefixMatching = (ah->requestedIPIsUsable && ah->numIPsCity > 0);
    } else if(g_queue_get_length(ah->candidatesCountryAndType) > 0) {
        candidates = ah->candidatesCountryAndType;
        useLongestPrefixMatching = (ah->requestedIPIsUsable && ah->numIPsCountryAndType > 0);
    } else if(g_queue_get_length(ah->candidatesCountry) > 0) {
        candidates = ah->candidatesCountry;
        useLongestPrefixMatching = (ah->requestedIPIsUsable && ah->numIPsCountry > 0);
    } else if(g_queue_get_length(ah->candidatesGeoAndType) > 0) {
        candidates = ah->candidatesGeoAndType;
        useLongestPrefixMatching = (ah->requestedIPIsUsable && ah->numIPsGeoAndType > 0);
    } else if(g_queue_get_length(ah->candidatesGeo) > 0) {
        candidates = ah->candidatesGeo;
        useLongestPrefixMatching = (ah->requestedIPIsUsable && ah->numIPsGeo > 0);
    } else if(g_queue_get_length(ah->candidatesType) > 0) {
        candidates = ah->candidatesType;
        useLongestPrefixMatching = (ah->requestedIPIsUsable && ah->numIPsType > 0);
    } else {
        candidates = ah->candidatesAll;
        useLongestPrefixMatching = (ipHint && ah->numIPsAll > 0);
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
    if(ah->candidatesCityAndType) {
        g_queue_free(ah->candidatesCityAndType);
    }
    if(ah->candidatesCity) {
        g_queue_free(ah->candidatesCity);
    }
    if(ah->candidatesCountryAndType) {
        g_queue_free(ah->candidatesCountryAndType);
    }
    if(ah->candidatesCountry) {
        g_queue_free(ah->candidatesCountry);
    }
    if(ah->candidatesGeoAndType) {
        g_queue_free(ah->candidatesGeoAndType);
    }
    if(ah->candidatesGeo) {
        g_queue_free(ah->candidatesGeo);
    }
    if(ah->candidatesType) {
        g_queue_free(ah->candidatesType);
    }
    if(ah->candidatesAll) {
        g_queue_free(ah->candidatesAll);
    }
    g_free(ah);

    return vertexIndex;
}

void topology_attach(Topology* top, Address* address, Random* randomSourcePool,
        gchar* ipHint, gchar* citycodeHint, gchar* countrycodeHint, gchar* geocodeHint, gchar* typeHint,
        guint64* bwDownOut, guint64* bwUpOut) {
    MAGIC_ASSERT(top);
    utility_assert(address);

    in_addr_t nodeIP = address_toNetworkIP(address);
    igraph_integer_t vertexIndex = _topology_findAttachmentVertex(top, randomSourcePool, nodeIP,
            ipHint, citycodeHint, countrycodeHint, geocodeHint, typeHint);

    /* attach it, i.e. store the mapping so we can route later */
    g_rw_lock_writer_lock(&(top->virtualIPLock));
    g_hash_table_replace(top->virtualIP, GUINT_TO_POINTER(nodeIP), GINT_TO_POINTER(vertexIndex));
    g_hash_table_replace(top->verticesWithAttachedHosts, GUINT_TO_POINTER(vertexIndex), GINT_TO_POINTER(vertexIndex));
    g_rw_lock_writer_unlock(&(top->virtualIPLock));

    const gchar* idStr = NULL;
    const gchar* ipStr = NULL;
    const gchar* citycodeStr = NULL;
    const gchar* countrycodeStr = NULL;
    const gchar* geocodeStr = NULL;
    const gchar* typeStr = NULL;
    gboolean found;

    _topology_lockGraph(top);

    /* give them the default cluster bandwidths if they asked */
    if(bwUpOut) {
        gdouble bandwidthUp;
        found = _topology_findVertexAttributeDouble(top, vertexIndex, VERTEX_ATTR_BANDWIDTHUP, &bandwidthUp);
        utility_assert(found);
        *bwUpOut = (guint64) bandwidthUp;
    }
    if(bwDownOut) {
        gdouble bandwidthDown;
        found = _topology_findVertexAttributeDouble(top, vertexIndex, VERTEX_ATTR_BANDWIDTHDOWN, &bandwidthDown);
        utility_assert(found);
        *bwDownOut = (guint64) bandwidthDown;
    }

    found = _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_ID, &idStr);
    utility_assert(found);

    /* these may fail and not set the string value if the vertex does not have the attribute.
     * that's ok though, glib will print null in its place in the format string below. */
    _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_IP, &ipStr);
    _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_CITYCODE, &citycodeStr);
    _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_COUNTRYCODE, &countrycodeStr);
    _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_GEOCODE, &geocodeStr);
    _topology_findVertexAttributeString(top, vertexIndex, VERTEX_ATTR_TYPE, &typeStr);

    _topology_unlockGraph(top);

    message("attached address '%s' to vertex %li ('%s') "
            "with attributes (ip=%s, citycode=%s, countrycode=%s, geocode=%s, type=%s) "
            "using hints (ip=%s, citycode=%s, countrycode=%s, geocode=%s, type=%s)",
            address_toHostIPString(address), (glong)vertexIndex, idStr,
            ipStr, citycodeStr, countrycodeStr, geocodeStr, typeStr,
            ipHint, citycodeHint, countrycodeHint, geocodeHint, typeHint);
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

    /* log all of the paths that we looked up for post analysis */
    _topology_logAllCachedPaths(top);

    /* clear the virtual ip table */
    g_rw_lock_writer_lock(&(top->virtualIPLock));
    if(top->virtualIP) {
        g_hash_table_destroy(top->virtualIP);
        top->virtualIP = NULL;
    }
    if(top->verticesWithAttachedHosts) {
        g_hash_table_destroy(top->verticesWithAttachedHosts);
        top->verticesWithAttachedHosts = NULL;
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
    top->verticesWithAttachedHosts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

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
        critical("we failed to create the simulation topology because we were unable to validate the topology graphml file");
        return NULL;
    }

    return top;
}
