/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Topology {
	/* the file path of the graphml file */
	GString* graphPath;

	/******/
	/* START global igraph lock - igraph is not thread-safe!*/
	GMutex graphLock;

	/* the imported igraph graph data - operations on it after initializations
	 * MUST be locked because igraph is not thread-safe! */
	igraph_t graph;

	/* the edge weights currently used when computing shortest paths.
	 * this is protected by the graph lock */
	igraph_vector_t* edgeWeights;

	/* keep track of how many, and how long we spend computing shortest paths,
	 * protected by the graph lock */
	gdouble shortestPathTotalTime;
	guint shortestPathCount;

	/* END global igraph lock - igraph is not thread-safe!*/
	/******/

	/* each connected virtual host is assigned to a PoI vertex. we store the mapping to the
	 * vertex index so we can correctly lookup the assigned edge when computing latency.
	 * virtualIP->vertexIndex (stored as pointer) */
	GHashTable* virtualIP;
	GRWLock virtualIPLock;

	/* cached latencies to avoid excessive shortest path lookups
	 * store a cache table for every connected address
	 * fromAddress->toAddress->Path* */
	GHashTable* pathCache;
	GRWLock pathCacheLock;

	/* graph properties of the imported graph */
	igraph_integer_t clusterCount;
	igraph_integer_t vertexCount;
	igraph_integer_t edgeCount;
	igraph_bool_t isConnected;
	igraph_bool_t isDirected;

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

static gboolean _topology_loadGraph(Topology* top) {
	MAGIC_ASSERT(top);
	/* initialize the built-in C attribute handler */
	igraph_attribute_table_t* oldHandler = igraph_i_set_attribute_table(&igraph_cattribute_table);

	/* get the file */
	FILE* graphFile = fopen(top->graphPath->str, "r");
	if(!graphFile) {
		critical("fopen returned NULL, problem opening graph file path '%s'", top->graphPath->str);
		return FALSE;
	}

	info("reading graphml topology graph at '%s'", top->graphPath->str);

	gint result = igraph_read_graph_graphml(&top->graph, graphFile, 0);
	fclose(graphFile);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_read_graph_graphml return non-success code %i", result);
		return FALSE;
	}

	info("successfully read graphml topology graph at '%s'", top->graphPath->str);

	return TRUE;
}

static gboolean _topology_checkGraphProperties(Topology* top) {
	MAGIC_ASSERT(top);
	gint result = 0;

	info("checking graph properties...");

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
		critical("topology must be but is not strongly connected", top->graphPath->str);
		return FALSE;
	}

	top->isDirected = igraph_is_directed(&top->graph);

	info("topology graph is %s and %s with %u %s",
			top->isDirected ? "directed" : "undirected",
			top->isConnected ? "strongly connected" : "disconnected",
			(guint)top->clusterCount, top->clusterCount == 1 ? "cluster" : "clusters");

	info("checking graph attributes...");

	/* now check list of all attributes */
	igraph_strvector_t gnames, vnames, enames;
	igraph_vector_t gtypes, vtypes, etypes;
	igraph_strvector_init(&gnames, 1);
	igraph_vector_init(&gtypes, 1);
	igraph_strvector_init(&vnames, igraph_vcount(&top->graph));
	igraph_vector_init(&vtypes, igraph_vcount(&top->graph));
	igraph_strvector_init(&enames, igraph_ecount(&top->graph));
	igraph_vector_init(&etypes, igraph_ecount(&top->graph));

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

	info("successfully verified graph attributes");

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

	info("checking graph vertices...");

	igraph_integer_t vertexCount = _topology_iterateAllVertices(top, _topology_checkGraphVerticesHelperHook, NULL);
	if(vertexCount < 0) {
		/* there was some kind of error */
		return FALSE;
	}

	top->vertexCount = igraph_vcount(&top->graph);
	if(top->vertexCount != vertexCount) {
		warning("igraph_vcount %f does not match iterator count %f", top->vertexCount, vertexCount);
	}

	info("%u graph vertices ok", (guint) top->vertexCount);

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

	info("checking graph edges...");

	igraph_integer_t edgeCount = _topology_iterateAllEdges(top, _topology_checkGraphEdgesHelperHook, NULL);
	if(edgeCount < 0) {
		/* there was some kind of error */
		return FALSE;
	}

	top->edgeCount = igraph_ecount(&top->graph);
	if(top->edgeCount != edgeCount) {
		warning("igraph_vcount %f does not match iterator count %f", top->edgeCount, edgeCount);
	}

	info("%u graph edges ok", (guint) top->edgeCount);

	return TRUE;
}

static gboolean _topology_checkGraph(Topology* top) {
	if(!_topology_checkGraphProperties(top) || !_topology_checkGraphVertices(top) ||
			!_topology_checkGraphEdges(top)) {
		return FALSE;
	}

	message("successfully parsed graphml at '%s' and validated topology: "
			"graph is %s with %u %s, %u %s, and %u %s",
			top->graphPath->str, top->isConnected ? "strongly connected" : "disconnected",
			(guint)top->clusterCount, top->clusterCount == 1 ? "cluster" : "clusters",
			(guint)top->vertexCount, top->vertexCount == 1 ? "vertex" : "vertices",
			(guint)top->edgeCount, top->edgeCount == 1 ? "edge" : "edges");

	return TRUE;
}

static gboolean _topology_extractEdgeWeights(Topology* top) {
	MAGIC_ASSERT(top);

	g_mutex_lock(&top->graphLock);

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
		g_mutex_unlock(&top->graphLock);
		critical("igraph_vector_init return non-success code %i", result);
		return FALSE;
	}

	/* use the 'latency' edge attribute as the edge weight */
	result = EANV(&top->graph, "latency", top->edgeWeights);
	g_mutex_unlock(&top->graphLock);
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

	message("path cache cleared, spent %f seconds computing %u shortest paths",
			top->shortestPathTotalTime, top->shortestPathCount);
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

	Path* path = path_new((gdouble) totalLatency, (gdouble) totalReliability);

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
		g_hash_table_replace(top->pathCache, GUINT_TO_POINTER(srcVertexIndex), sourceCache);
	}

	g_hash_table_replace(sourceCache, GINT_TO_POINTER(dstVertexIndex), path);

	g_rw_lock_writer_unlock(&(top->pathCacheLock));
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
	GString* pathString = g_string_new(NULL);
	gint result = 0;

	g_mutex_lock(&top->graphLock);

	/* always include reliability for the src vertex */
	igraph_real_t totalLatency = 0.0;
	igraph_real_t totalReliability = (1.0f - VAN(&top->graph, "packetloss", srcVertexIndex));

	const gchar* srcIDStr = VAS(&top->graph, "id", srcVertexIndex);
	g_string_append_printf(pathString, "%s", srcIDStr);

	const gchar* dstIDStr = srcIDStr;
	igraph_integer_t dstVertexIndex = srcVertexIndex;

	glong nVertices = igraph_vector_size(resultPathVertices);

	if(nVertices == 0) {
		/* path to self */
		totalLatency = 1.0;
	} else if(nVertices == 1) {
		/* src and dst are attached to the same graph vertex */
		totalLatency = 10.0;
	} else {
		/* get the destination */
		dstVertexIndex = (igraph_integer_t) igraph_vector_tail(resultPathVertices);
		totalReliability *= (1.0f - VAN(&top->graph, "packetloss", dstVertexIndex));

		/* now get latency and reliability from each edge in the path */
		igraph_integer_t fromVertexIndex = srcVertexIndex, toVertexIndex = 0,  edgeIndex = 0;

		/* now iterate the edges in the path */
		for (gint i = 1; i < nVertices; i++) {
			/* get the edge */
			toVertexIndex = igraph_vector_e(resultPathVertices, i);
#ifndef IGRAPH_VERSION
			result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, (igraph_bool_t)TRUE);
#else
			result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, (igraph_bool_t)TRUE, (igraph_bool_t)TRUE);
#endif
			if(result != IGRAPH_SUCCESS) {
				g_mutex_unlock(&top->graphLock);
				critical("igraph_get_eid return non-success code %i for edge between "
						"vertex %i and %i", result, (gint) fromVertexIndex, (gint) toVertexIndex);
				return FALSE;
			}

			/* get edge properties from graph */
			igraph_real_t edgeLatency = EAN(&top->graph, "latency", edgeIndex);
			totalLatency += edgeLatency;
			igraph_real_t edgeReliability = 1.0f - EAN(&top->graph, "packetloss", edgeIndex);
			totalReliability *= edgeReliability;

			/* accumulate path information */
			const gchar* toIDStr = VAS(&top->graph, "id", toVertexIndex);
			g_string_append_printf(pathString, "--[%f,%f]-->%s", edgeLatency, edgeReliability, toIDStr);
			dstIDStr = toIDStr;

			/* update for next edge */
			fromVertexIndex = toVertexIndex;
		}
	}

	g_mutex_unlock(&top->graphLock);

	//TODO debug level
	info("shortest path %s-->%s is %f ms with %f loss, path: %s", srcIDStr, dstIDStr,
			totalLatency, 1-totalReliability, pathString->str);

	g_string_free(pathString, TRUE);

	/* cache the latency and reliability we just computed */
	_topology_storePathInCache(top, srcVertexIndex, dstVertexIndex, totalLatency, totalReliability);

	return TRUE;
}

static gboolean _topology_computeSourcePaths(Topology* top, igraph_integer_t srcVertexIndex) {
	MAGIC_ASSERT(top);

	if(srcVertexIndex < 0) {
		/* not connected to a vertex */
		error("_topology_computeSourcePaths invalid source vertex %i", (gint)srcVertexIndex);
		return FALSE;
	}

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

	GList* target = attachedTargets;
	for(gint position = 0; position < numTargets; position++) {
		/* set each vertex index as a destination for dijkstra */
		utility_assert(target != NULL);
		igraph_integer_t vertexIndex = (igraph_integer_t) GPOINTER_TO_INT(target->data);
		igraph_vector_set(&dstVertexIndexSet, position, (igraph_real_t) vertexIndex);
		target = target->next;

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

	g_mutex_lock(&top->graphLock);

	const gchar* srcIDStr = VAS(&top->graph, "id", srcVertexIndex);
	debug("computing shortest paths from source vertex %li (%s)", (glong)srcVertexIndex, srcIDStr);

	/* time the dijkstra algorithm */
	GTimer* pathTimer = g_timer_new();

	/* run dijkstra's shortest path algorithm */
#ifndef IGRAPH_VERSION
	result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths,
			srcVertexIndex, igraph_vss_vector(&dstVertexIndexSet), top->edgeWeights, IGRAPH_OUT);
#else
	result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths, NULL,
			srcVertexIndex, igraph_vss_vector(&dstVertexIndexSet), top->edgeWeights, IGRAPH_OUT);
#endif

	/* track the time spent running the algorithm */
	gdouble elapsedSeconds = g_timer_elapsed(pathTimer, NULL);

	g_mutex_unlock(&top->graphLock);

	g_timer_destroy(pathTimer);
	top->shortestPathTotalTime += elapsedSeconds;
	top->shortestPathCount++;

	if(result != IGRAPH_SUCCESS) {
		critical("igraph_get_shortest_paths_dijkstra return non-success code %i", result);
		return FALSE;
	}

	gboolean allSuccess = TRUE;

	/* go through the result paths for all targets */
	for(gint position = 0; position < numTargets; position++) {
		/* handle the path to the destination at this position */
		igraph_vector_t* resultPathVertices = igraph_vector_ptr_e(&resultPaths, position);

		gboolean success = _topology_computeSourcePathsHelper(top, srcVertexIndex, resultPathVertices);
		if(!success) {
			allSuccess = FALSE;
		}

		igraph_vector_destroy(resultPathVertices);
		g_free(resultPathVertices);
	}

	/* clean up */
	igraph_vector_ptr_destroy(&resultPaths);
	igraph_vector_destroy(&dstVertexIndexSet);

	/* success */
	return allSuccess;
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
		/* cache miss, compute all source shortest latency paths */
		gboolean success = _topology_computeSourcePaths(top, srcVertexIndex);
		if(success) {
		    path = _topology_getPathFromCache(top, srcVertexIndex, dstVertexIndex);
		}
	}

	if(!path) {
		/* some error computing or caching path */
		error("unable to compute or find cached path between node %s at vertex %i "
				"and node %s at vertex %i", (gint)srcVertexIndex, address_toString(srcAddress),
				(gint)dstVertexIndex, address_toString(dstAddress));
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
		g_mutex_lock(&top->graphLock);
		const gchar* ipStr = VAS(&top->graph, "ip", vertexIndex);
		g_mutex_unlock(&top->graphLock);
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
	g_mutex_lock(&top->graphLock);
	_topology_iterateAllVertices(top, (VertexNotifyFunc) _topology_findAttachmentVertexHelperHook, ah);
	g_mutex_unlock(&top->graphLock);

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

	g_mutex_lock(&top->graphLock);

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

	g_mutex_unlock(&top->graphLock);

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

	/* clear the graph */
	g_mutex_lock(&top->graphLock);

	if(top->graphPath) {
		g_string_free(top->graphPath, TRUE);
	}

	if(top->edgeWeights) {
		igraph_vector_destroy(top->edgeWeights);
		g_free(top->edgeWeights);
	}
	top->edgeWeights = NULL;

	igraph_destroy(&top->graph);

	g_mutex_unlock(&top->graphLock);
	g_mutex_clear(&(top->graphLock));

	MAGIC_CLEAR(top);
	g_free(top);
}

Topology* topology_new(gchar* graphPath) {
	utility_assert(graphPath);
	Topology* top = g_new0(Topology, 1);
	MAGIC_INIT(top);

	top->graphPath = g_string_new(graphPath);
	top->virtualIP = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

	g_mutex_init(&(top->graphLock));
	g_rw_lock_init(&(top->virtualIPLock));
	g_rw_lock_init(&(top->pathCacheLock));

	/* first read in the graph and make sure its formed correctly,
	 * then setup our edge weights for shortest path */
	if(!_topology_loadGraph(top) || !_topology_checkGraph(top) ||
			!_topology_extractEdgeWeights(top)) {
		topology_free(top);
		return NULL;
	}

	return top;
}
