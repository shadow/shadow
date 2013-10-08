/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef enum _ClusterAttribute ClusterAttribute;
enum _ClusterAttribute {
	CA_PACKETLOSS, CA_BANDWIDTHUP, CA_BANDWIDTHDOWN,
};

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
	igraph_vector_t* currentEdgeWeights;

	/* keep track of how long we spend computing shortest paths,
	 * protected by the graph lock */
	gdouble shortestPathTotalTime;

	/* END global igraph lock - igraph is not thread-safe!*/
	/******/

	/* each connected virtual host is assigned to a PoI. we store the mapping
	 * so we can correctly lookup the assigned edge when computing latency. */
	GHashTable* poiIPToVertexIndex; /* read-only after initialization! */

	GHashTable* virtualIPToPoIIP;
	GHashTable* virtualIPToCluster;
	GRWLock virtualIPLock;

	/* stores the mapping between geocode (state/prov/country) and its cluster properties.
	 * this is useful when placing hosts without an IP in the topology */
	GHashTable* geocodeToCluster; /* read-only after initialization! */

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

	/* FIXME hack for broken graph attributes */
	const gchar* plossStr;
	const gchar* bwupStr;
	const gchar* bwdownStr;
	/* end hack */

	MAGIC_DECLARE;
};

typedef void (*EdgeNotifyFunc)(Topology* top, igraph_integer_t edgeIndex);
typedef void (*VertexNotifyFunc)(Topology* top, igraph_integer_t vertexIndex);

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

	info("graph is %s with %u %s",
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

	/* make sure we can get our required graph attributes */
	/* FIXME re-enable this after graph attribute hack is removed
	const gchar* plossStr = GAS(&top->graph, "packetloss");
	debug("found graph attribute packetloss=%s", plossStr);
	const gchar* bwupStr = GAS(&top->graph, "bandwidthup");
	debug("found graph attribute bandwidthup=%s", bwupStr);
	const gchar* bwdownStr = GAS(&top->graph, "bandwidthdown");
	debug("found graph attribute bandwidthdown=%s", bwdownStr);
	*/

	info("successfully verified graph attributes");

	return TRUE;
}

static void _topology_checkGraphVerticesHelperHook(Topology* top, igraph_integer_t vertexIndex) {
	MAGIC_ASSERT(top);

	/* get vertex attributes: S for string and N for numeric */
	const gchar* idStr = VAS(&top->graph, "id", vertexIndex);
	const gchar* nodeTypeStr = VAS(&top->graph, "nodetype", vertexIndex);
	const gchar* geocodesStr = VAS(&top->graph, "geocodes", vertexIndex);
	gdouble asNumber = VAN(&top->graph, "asn", vertexIndex);

	debug("found vertex %li (%s), nodetype=%s geocodes=%s asn=%i",
			(glong)vertexIndex, idStr, nodeTypeStr, geocodesStr, asNumber);
}

static igraph_integer_t _topology_iterateAllVertices(Topology* top, VertexNotifyFunc hook) {
	MAGIC_ASSERT(top);
	utility_assert(hook);

	/* initialize our vertex set to select from all vertices in the graph */
	igraph_vs_t allVertices;
	gint result = igraph_vs_all(&allVertices);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vs_all return non-success code %i", result);
		return -1;
	}

	/* we will iterate through the vertices */
	igraph_vit_t vertexIterator;
	result = igraph_vit_create(&top->graph, allVertices, &vertexIterator);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vit_create return non-success code %i", result);
		return -1;
	}

	/* count the vertices as we iterate */
	igraph_integer_t vertexCount = 0;
	while (!IGRAPH_VIT_END(vertexIterator)) {
		long int vertexIndex = IGRAPH_VIT_GET(vertexIterator);

		/* FIXME hack for broken graph attributes */
		const gchar* idStr = VAS(&top->graph, "id", vertexIndex);
		if(!g_ascii_strcasecmp(idStr, "dummynode")) {
			if(!top->plossStr) {
				top->plossStr = VAS(&top->graph, "packetloss", vertexIndex);
			}
			if(!top->bwupStr) {
				top->bwupStr = VAS(&top->graph, "bandwidthup", vertexIndex);
			}
			if(!top->bwdownStr) {
				top->bwdownStr = VAS(&top->graph, "bandwidthdown", vertexIndex);
			}
			vertexCount++;
			IGRAPH_VIT_NEXT(vertexIterator);
			continue;
		}
		/* end hack */

		/* call the hook function for each edge */
		hook(top, (igraph_integer_t) vertexIndex);

		vertexCount++;
		IGRAPH_VIT_NEXT(vertexIterator);
	}

	/* clean up */
	igraph_vit_destroy(&vertexIterator);
	igraph_vs_destroy(&allVertices);

	return vertexCount;
}

static gboolean _topology_checkGraphVertices(Topology* top) {
	MAGIC_ASSERT(top);

	info("checking graph vertices...");

	igraph_integer_t vertexCount = _topology_iterateAllVertices(top, _topology_checkGraphVerticesHelperHook);
	if(vertexCount < 0) {
		/* there was some kind of error */
		return FALSE;
	}

	top->vertexCount = igraph_vcount(&top->graph);
	if(top->vertexCount != vertexCount) {
		warning("igraph_vcount does not match iterator count");
	}

	info("%u graph vertices ok", (guint) top->vertexCount);

	return TRUE;
}

static void _topology_checkGraphEdgesHelperHook(Topology* top, igraph_integer_t edgeIndex) {
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
	const gchar* latenciesStr = EAS(&top->graph, "latencies", edgeIndex);

	debug("found edge %li from vertex %li (%s) to vertex %li (%s) latencies=%s",
			(glong)edgeIndex, (glong)fromVertexIndex, fromIDStr, (glong)toVertexIndex, toIDStr, latenciesStr);
}

static igraph_integer_t _topology_iterateAllEdges(Topology* top, EdgeNotifyFunc hook) {
	MAGIC_ASSERT(top);
	utility_assert(hook);

	/* our selector will consider all edges, ordered by the igraph edge indices */
	igraph_es_t allEdges;
	gint result = igraph_es_all(&allEdges, IGRAPH_EDGEORDER_ID);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_es_all return non-success code %i", result);
		return -1;
	}

	/* we will iterate through the edges */
	igraph_eit_t edgeIterator;
	result = igraph_eit_create(&top->graph, allEdges, &edgeIterator);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_eit_create return non-success code %i", result);
		return -1;
	}

	/* count the edges as we iterate */
	igraph_integer_t edgeCount = 0;
	while (!IGRAPH_EIT_END(edgeIterator)) {
		long int edgeIndex = IGRAPH_EIT_GET(edgeIterator);

		/* call the hook function for each edge */
		hook(top, (igraph_integer_t) edgeIndex);

		edgeCount++;
		IGRAPH_EIT_NEXT(edgeIterator);
	}

	igraph_eit_destroy(&edgeIterator);
	igraph_es_destroy(&allEdges);

	return edgeCount;
}

static gboolean _topology_checkGraphEdges(Topology* top) {
	MAGIC_ASSERT(top);

	info("checking graph edges...");

	igraph_integer_t edgeCount = _topology_iterateAllEdges(top, _topology_checkGraphEdgesHelperHook);
	if(edgeCount < 0) {
		/* there was some kind of error */
		return FALSE;
	}

	top->edgeCount = igraph_ecount(&top->graph);
	if(top->edgeCount != edgeCount) {
		warning("igraph_vcount does not match iterator count");
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

/* locked for writes to currentEdgeWeights */
static void _topology_updateEdgeWeightsHelperHook(Topology* top, igraph_integer_t edgeIndex) {
	/* get the array of latencies for this edge */
	const gchar* latenciesStr = EAS(&top->graph, "latencies", edgeIndex);
	gchar** latencyParts = g_strsplit(latenciesStr, ",", 0);

	/* get length of the latency array */
	gint length = 0;
	while(latencyParts[length] != NULL) {
		length++;
	}

	/* select a random index from the latency list */
	gdouble randomDouble = worker_nextRandomDouble();
	gint randomIndex = (gint)(length * randomDouble) - 1;
	if(randomIndex < 0) {
		randomIndex = 0;
	}

	/* set the selected latency as the current weight for this edge */
	igraph_real_t latency = (igraph_real_t) atof(latencyParts[randomIndex]);
	/* we can just push back because we are iterating by IGRAPH_EDGEORDER_ID */
	igraph_vector_push_back(top->currentEdgeWeights, latency);

	g_strfreev(latencyParts);
}

static gboolean _topology_updateEdgeWeights(Topology* top) {
	MAGIC_ASSERT(top);

	g_mutex_lock(&top->graphLock);

	/* create new or clear existing edge weights */
	if(!top->currentEdgeWeights) {
		top->currentEdgeWeights = g_new0(igraph_vector_t, 1);
	} else {
		igraph_vector_destroy(top->currentEdgeWeights);
		memset(top->currentEdgeWeights, 0, sizeof(igraph_vector_t));
	}

	/* now we have fresh memory */
	gint result = igraph_vector_init(top->currentEdgeWeights, 0);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vector_init return non-success code %i", result);
		return FALSE;
	}

	igraph_integer_t edgeCount = _topology_iterateAllEdges(top, _topology_updateEdgeWeightsHelperHook);

	g_mutex_unlock(&top->graphLock);

	if(edgeCount < 0) {
		/* there was some kind of error */
		return FALSE;
	}

	/* instead of iterating, we could do the following if we had a single latency edge attribute
	igraph_vector_t edge_weights;
	igraph_vector_init(&edge_weights, 0);
	igraph_es_t all_edges;
	igraph_es_all(&all_edges, IGRAPH_EDGEORDER_ID);
	igraph_cattribute_EANV(&top->graph, "latency", all_edges, &edge_weights);
	*/

	return TRUE;
}

static void _topology_extractPointsOfInterestHelperHook(Topology* top, igraph_integer_t vertexIndex) {
	MAGIC_ASSERT(top);

	/* get vertex attributes: S for string and N for numeric */
	const gchar* idStr = VAS(&top->graph, "id", vertexIndex);
	const gchar* nodeTypeStr = VAS(&top->graph, "nodetype", vertexIndex);
	const gchar* geocodesStr = VAS(&top->graph, "geocodes", vertexIndex);

	if(!g_ascii_strcasecmp(nodeTypeStr, "relay") || !g_ascii_strcasecmp(nodeTypeStr, "server")) {
		/* this is a point of interest (poi) not a point of presence (pop) */
		in_addr_t networkIP = address_stringToIP(idStr);
		if(networkIP == INADDR_NONE) {
			error("graph topology error: points of interest (nodes that are not 'pop's) should have IP address as ID");
			return;
		}

		/* this is a PoI where virtual hosts will sit */
		g_hash_table_replace(top->poiIPToVertexIndex, GUINT_TO_POINTER(networkIP), GINT_TO_POINTER(vertexIndex));

		/* add it to the list of vertices by its geoclusters, for assigning hosts randomly later */
		gchar** geocodeParts = g_strsplit(geocodesStr, ",", 0);
		for(gint i = 0; geocodeParts[i] != NULL; i++) {
			Cluster* cluster = g_hash_table_lookup(top->geocodeToCluster, geocodeParts[i]);
			if(cluster) {
				cluster_addPoI(cluster, networkIP);
			}
		}
		g_strfreev(geocodeParts);
	}
}

static gboolean _topology_extractPointsOfInterest(Topology* top) {
	MAGIC_ASSERT(top);

	igraph_integer_t edgeCount = _topology_iterateAllVertices(top, _topology_extractPointsOfInterestHelperHook);
	if(edgeCount < 0) {
		/* there was some kind of error */
		return FALSE;
	} else {
		return TRUE;
	}
}

static void _topology_extractClustersHelper(Topology* top, const gchar* clusterStr, ClusterAttribute type) {
	gchar** listParts = g_strsplit(clusterStr, ",", 0);
	for(gint i = 0; listParts[i] != NULL; i++) {
		gchar** parts = g_strsplit(listParts[i], "=", 0);

		gchar* geocode = parts[0];
		gchar* value = parts[1];
		utility_assert(geocode && value);

		Cluster* cluster = g_hash_table_lookup(top->geocodeToCluster, geocode);
		if(!cluster) {
			gchar* newgeocode = g_strdup(geocode);
			cluster = cluster_new(newgeocode);
			g_hash_table_replace(top->geocodeToCluster, newgeocode, cluster);
		}

		if(type == CA_PACKETLOSS) {
			cluster_setPacketLoss(cluster, (gdouble) atof(value));
		} else if (type == CA_BANDWIDTHUP) {
			cluster_setBandwidthUp(cluster, (guint) atoi(value));
		} else if (type == CA_BANDWIDTHDOWN) {
			cluster_setBandwidthDown(cluster, (guint) atoi(value));
		}

		g_strfreev(parts);
	}
	g_strfreev(listParts);
}

static void _topology_extractClusters(Topology* top) {
	MAGIC_ASSERT(top);

	/* FIXME re-enable after graph attribute hack is removed
	const gchar* plossStr = GAS(&top->graph, "packetloss");
	const gchar* bwupStr = GAS(&top->graph, "bandwidthup");
	const gchar* bwdownStr = GAS(&top->graph, "bandwidthdown");
	*/
	const gchar* plossStr = top->plossStr;
	const gchar* bwupStr = top->bwupStr;
	const gchar* bwdownStr = top->bwdownStr;

	_topology_extractClustersHelper(top, plossStr, CA_PACKETLOSS);
	_topology_extractClustersHelper(top, bwupStr, CA_BANDWIDTHUP);
	_topology_extractClustersHelper(top, bwdownStr, CA_BANDWIDTHDOWN);

	info("found %u geocode clusters", g_hash_table_size(top->geocodeToCluster));
}

static gboolean _topology_setupGraph(Topology* top) {
	MAGIC_ASSERT(top);

	_topology_extractClusters(top);

	if(!_topology_extractPointsOfInterest(top) || !_topology_updateEdgeWeights(top)) {
		return FALSE;
	} else {
		return TRUE;
	}
}

static void _topology_clearCache(Topology* top) {
	MAGIC_ASSERT(top);
	g_rw_lock_writer_lock(&(top->pathCacheLock));
	if(top->pathCache) {
		g_hash_table_destroy(top->pathCache);
		top->pathCache = NULL;
	}
	g_rw_lock_writer_unlock(&(top->pathCacheLock));

	message("path cache cleared, spent %f seconds computing shortest paths", top->shortestPathTotalTime);
}

static Path* _topology_getPathFromCache(Topology* top, Address* source, Address* destination) {
	MAGIC_ASSERT(top);

	Path* path = NULL;
	g_rw_lock_reader_lock(&(top->pathCacheLock));

	if(top->pathCache) {
		/* look for the source first level cache */
		ShadowID srcID = address_getID(source);
		gpointer sourceCache = g_hash_table_lookup(top->pathCache, GUINT_TO_POINTER(srcID));

		if(sourceCache) {
			/* check for the path to destination in source cache */
			ShadowID dstID = address_getID(destination);
			path = g_hash_table_lookup(sourceCache, GUINT_TO_POINTER(dstID));
		}
	}

	g_rw_lock_reader_unlock(&(top->pathCacheLock));

	/* NULL if cache miss */
	return path;
}

static void _topology_storePathInCache(Topology* top, Address* source, Address* destination,
		igraph_real_t latency, igraph_real_t reliability) {
	MAGIC_ASSERT(top);

	ShadowID srcID = address_getID(source);
	ShadowID dstID = address_getID(destination);

	g_rw_lock_writer_lock(&(top->pathCacheLock));

	/* create latency cache on the fly */
	if(!top->pathCache) {
		/* stores hash tables for source address caches */
		top->pathCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_hash_table_destroy);
	}

	GHashTable* sourceCache = g_hash_table_lookup(top->pathCache, GUINT_TO_POINTER(srcID));
	if(!sourceCache) {
		/* dont have a cache for this source yet, create one now */
		sourceCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)path_free);
		g_hash_table_replace(top->pathCache, GUINT_TO_POINTER(srcID), sourceCache);
	}

	Path* path = path_new((gdouble) latency, (gdouble) reliability);
	g_hash_table_replace(sourceCache, GUINT_TO_POINTER(dstID), path);

	g_rw_lock_writer_unlock(&(top->pathCacheLock));
}

static igraph_integer_t _topology_getConnectedVertexIndex(Topology* top, Address* address) {
	MAGIC_ASSERT(top);

	in_addr_t ip = address_toNetworkIP(address);

	/* convert the virtual ip to a PoI ip */
	g_rw_lock_reader_lock(&(top->virtualIPLock));
	gpointer poiIPPtr = g_hash_table_lookup(top->virtualIPToPoIIP, GUINT_TO_POINTER(ip));
	g_rw_lock_reader_unlock(&(top->virtualIPLock));

	/* now get the PoI vertex index from the pop IP */
	gpointer vertexIndexPtr = NULL;
	gboolean found = g_hash_table_lookup_extended(top->poiIPToVertexIndex, poiIPPtr, NULL, &vertexIndexPtr);

	if(!poiIPPtr || !found) {
		warning("address %s is not connected to the topology", address_toHostIPString(address));
		return (igraph_integer_t) -1;
	}

	return (igraph_integer_t) GPOINTER_TO_INT(vertexIndexPtr);
}

/* unfortunately, igraph is not thread safe, so this function must be locked */
static gboolean _topology_computePath(Topology* top, Address* srcAddress, Address* dstAddress) {
	MAGIC_ASSERT(top);

	igraph_integer_t srcVertexIndex = _topology_getConnectedVertexIndex(top, srcAddress);
	igraph_integer_t dstVertexIndex = _topology_getConnectedVertexIndex(top, dstAddress);
	if(srcVertexIndex < 0 || dstVertexIndex < 0) {
		/* not connected to a vertex */
		return FALSE;
	}

	const gchar* srcIDStr = VAS(&top->graph, "id", srcVertexIndex);
	const gchar* dstIDStr = VAS(&top->graph, "id", dstVertexIndex);

	debug("computing shortest path between vertex %li (%s) and vertex %li (%s)",
			(glong)srcVertexIndex, srcIDStr, (glong)dstVertexIndex, dstIDStr);

	/* setup the destination as a vertex selector with one possible vertex */
	igraph_vs_t dstVertexSet;
	gint result = igraph_vs_1(&dstVertexSet, dstVertexIndex);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vs_1 return non-success code %i", result);
		return FALSE;
	}

	/* initialize our result vector where the 1 resulting path will be stored */
	igraph_vector_ptr_t resultPaths;
	result = igraph_vector_ptr_init(&resultPaths, 1);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vector_ptr_init return non-success code %i", result);
		return FALSE;
	}

	/* initialize our 1 result element to hold the path vertices */
	igraph_vector_t resultPathVertices;
	result = igraph_vector_init(&resultPathVertices, 0);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vector_init return non-success code %i", result);
		return FALSE;
	}

	/* assign our element to the result vector */
	igraph_vector_ptr_set(&resultPaths, 0, &resultPathVertices);
	utility_assert(&resultPathVertices == igraph_vector_ptr_e(&resultPaths, 0));

	GTimer* pathTimer = g_timer_new();
	/* run dijkstra's shortest path algorithm */
#ifndef IGRAPH_VERSION
	result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths,
			srcVertexIndex, dstVertexSet, top->currentEdgeWeights, IGRAPH_OUT);
#else
	result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths, NULL,
			srcVertexIndex, dstVertexSet, top->currentEdgeWeights, IGRAPH_OUT);
#endif
	gdouble elapsedSeconds = g_timer_elapsed(pathTimer, NULL);

	g_timer_destroy(pathTimer);
	top->shortestPathTotalTime += elapsedSeconds;

	if(result != IGRAPH_SUCCESS) {
		critical("igraph_get_shortest_paths_dijkstra return non-success code %i", result);
		return FALSE;
	}

	/* there are multiple chances to drop a packet here:
	 * psrc : loss rate from source vertex
	 * plink ... : loss rate on the links between source-vertex and destination-vertex
	 * pdst : loss rate from destination vertex
	 *
	 * The reliability is then the combination of the probability
	 * that its not dropped in each case:
	 * P = ((1-psrc)(1-plink)...(1-pdst))
	 */

	/* now get edge latencies and loss rates from the list of vertices (igraph fail!) */
	GString* pathString = g_string_new(NULL);
	igraph_real_t totalLatency = 0, edgeLatency = 0;
	igraph_real_t totalReliability = 1, edgeReliability = 1;
	igraph_integer_t edgeIndex = 0, fromVertexIndex = 0, toVertexIndex = 0;

	/* reliability for the src and dst vertices */
	in_addr_t srcNodeIP = address_toNetworkIP(srcAddress);
	in_addr_t dstNodeIP = address_toNetworkIP(dstAddress);

	g_rw_lock_reader_lock(&(top->virtualIPLock));
	Cluster* srcCluster = g_hash_table_lookup(top->virtualIPToCluster, GUINT_TO_POINTER(srcNodeIP));
	Cluster* dstCluster = g_hash_table_lookup(top->virtualIPToCluster, GUINT_TO_POINTER(dstNodeIP));
	g_rw_lock_reader_unlock(&(top->virtualIPLock));

	if(srcCluster) {
		totalReliability *= (1.0 - cluster_getPacketLoss(srcCluster));
	}
	if(dstCluster) {
		totalReliability *= (1.0 - cluster_getPacketLoss(dstCluster));
	}

	glong nVertices = igraph_vector_size(&resultPathVertices);

	/* the first vertex is our starting point
	 * igraph_vector_size can be 0 for paths to ourself */
	if(nVertices > 0) {
		fromVertexIndex = VECTOR(resultPathVertices)[0];
		const gchar* fromIDStr = VAS(&top->graph, "id", fromVertexIndex);
		g_string_append_printf(pathString, "%s", fromIDStr);
	}
	if(nVertices < 2){
		/* we have no edges, src and dst are in the same vertex, or path to self */
		totalLatency = 1.0;
	} else {
		/* iterate the edges in the path and sum the latencies */
		for (gint i = 1; i < nVertices; i++) {
			/* get the edge */
			toVertexIndex = VECTOR(resultPathVertices)[i];
	#ifndef IGRAPH_VERSION
			result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, (igraph_bool_t)TRUE);
	#else
			result = igraph_get_eid(&top->graph, &edgeIndex, fromVertexIndex, toVertexIndex, (igraph_bool_t)TRUE, (igraph_bool_t)TRUE);
	#endif
			if(result != IGRAPH_SUCCESS) {
				warning("igraph_get_eid return non-success code %i", result);
				return FALSE;
			}

			/* add edge latency */
			edgeLatency = VECTOR(*(top->currentEdgeWeights))[(gint)edgeIndex];
			totalLatency += edgeLatency;

			// TODO add actual edge reliability
			edgeReliability = 1.0;
			totalReliability *= edgeReliability;

			/* accumulate path information */
			const gchar* toIDStr = VAS(&top->graph, "id", toVertexIndex);
			g_string_append_printf(pathString, "--[%f,%f]-->%s", edgeLatency, edgeReliability, toIDStr);

			/* update for next edge */
			fromVertexIndex = toVertexIndex;
		}
	}

	debug("shortest path %s-->%s is %f ms with %f loss, path: %s", srcIDStr, dstIDStr,
			totalLatency, 1-totalReliability, pathString->str);

	/* clean up */
	g_string_free(pathString, TRUE);
	igraph_vector_clear(&resultPathVertices);
	igraph_vector_ptr_destroy(&resultPaths);

	/* cache the latency and reliability we just computed */
	_topology_storePathInCache(top, srcAddress, dstAddress, totalLatency, totalReliability);

	/* success */
	return TRUE;
}

static gboolean _topology_getPathEntry(Topology* top, Address* srcAddress, Address* dstAddress,
		gdouble* latency, gdouble* reliability) {
	MAGIC_ASSERT(top);

	/* check for a cache hit */
	Path* path = _topology_getPathFromCache(top, srcAddress, dstAddress);
	if(!path) {
		/* cache miss, compute the path using shortest latency path from src to dst */
		g_mutex_lock(&top->graphLock);
		gboolean isSuccess = _topology_computePath(top, srcAddress, dstAddress);
		g_mutex_unlock(&top->graphLock);
		utility_assert(isSuccess);
		path = _topology_getPathFromCache(top, srcAddress, dstAddress);
	}

	if(path) {
		if(latency) {
			*latency = path_getLatency(path);
		}
		if(reliability) {
			*reliability = path_getReliability(path);
		}
		return TRUE;
	} else {
		/* some error computing or caching path */
		return FALSE;
	}
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

void topology_connect(Topology* top, Address* address, Random* randomSourcePool,
		gchar* ipHint, gchar* clusterHint, guint64* bwDownOut, guint64* bwUpOut) {
	MAGIC_ASSERT(top);
	utility_assert(address);

	// XXX we can connect to non-PoI pops too, as they have a larger list of countries!

	in_addr_t nodeIP = address_toNetworkIP(address);
	in_addr_t poiIP = 0;
	Cluster* cluster = NULL;

	/* we need to connect a host's network interface to the topology so that it can route.
	 * the address of the interface is required, but the remaining params are optional.
	 *
	 * there are several ways to connect; we use this logic:
	 *  1. if the ipHint (from hosts.xml) is given and
	 *   1a. it specifies an existing PoI in the topology, we connect there
	 *   1b. it does not specify an existing PoI, then we check the clusterHint, and
	 *    1aa. if it exists and contains PoIs, we connect to the longest prefix matched PoI
	 *         from the existing cluster
	 *    1ab. if it exists but does not contain PoIs, we connect to the longest prefix matched
	 *         PoI from the list of all PoIs, but use the cluster bandwidth and loss attributes
	 *    1ac. if NULL or it does not exist, we connect to the global longest prefix matched PoI
	 *  2. if ipHint is NULL, we check the clusterHint, and
	 *   2a. if it exists and contains PoIs, we select one at random
	 *   2b. if it exists but does not contain PoIs, we choose a random PoI, but use the cluster
	 *       bandwidth attributes
	 *   2c. if NULL or it does not exist, we choose a random PoI and its cluster
	 */

//	if(ipHint) { XXX
	if(FALSE) {

	} else {
		/* no ipHint, case 2 */
		if(clusterHint) {
			cluster = g_hash_table_lookup(top->geocodeToCluster, clusterHint);
			if(cluster && cluster_getPoICount(cluster) > 0) {
				/* case 2a */
				poiIP = cluster_getRandomPoI(cluster, randomSourcePool);
			}
		}

		if(!poiIP) {
			/* case 2b, choose a random PoI */
			guint tableSize = g_hash_table_size(top->poiIPToVertexIndex);
			if(tableSize < 1) {
				error("the topology contains no points of interest to which we can assign hosts");
			}

			GList* keyList = g_hash_table_get_keys(top->poiIPToVertexIndex);
			guint keyIndexRange = tableSize - 1;

			gdouble randomDouble = random_nextDouble(randomSourcePool);
			guint randomIndex = (guint) round((gdouble)(keyIndexRange * randomDouble));
			gpointer randomKeyPtr = g_list_nth_data(keyList, randomIndex);

			/* sanity checks */
			utility_assert(randomKeyPtr);
			gpointer randomVertexIndexPtr = NULL;
			gboolean vertexIndexIsFound = g_hash_table_lookup_extended(top->poiIPToVertexIndex, randomKeyPtr, NULL, &randomVertexIndexPtr);
			utility_assert(vertexIndexIsFound);

			g_list_free(keyList);

			poiIP = (in_addr_t) GPOINTER_TO_UINT(randomKeyPtr);

			if(!cluster) {
				/* case 2c, choose the random PoIs cluster too */
				igraph_integer_t randomVertexIndex = (igraph_integer_t) GPOINTER_TO_INT(randomVertexIndexPtr);
				const gchar* geocodesStr = VAS(&top->graph, "geocodes", randomVertexIndex);
				cluster = g_hash_table_lookup(top->geocodeToCluster, geocodesStr);
			}
		}
	}

	/* make sure we found somewhere to attach it */
	if(!poiIP || !cluster) {
		gchar* addressStr = address_toHostIPString(address);
		error("unable to assign host address %s to a PoI/Cluster using ipHint %s and clusterHint %s",
				addressStr, ipHint ? ipHint : "(null)", clusterHint ? clusterHint : "(null)");
	}
	utility_assert(poiIP && cluster);

	/* attach it, i.e. store the mapping so we can route later */
	cluster_ref(cluster);
	g_rw_lock_writer_lock(&(top->virtualIPLock));
	g_hash_table_replace(top->virtualIPToPoIIP, GUINT_TO_POINTER(nodeIP), GUINT_TO_POINTER(poiIP));
	g_hash_table_replace(top->virtualIPToCluster, GUINT_TO_POINTER(nodeIP), cluster);
	g_rw_lock_writer_unlock(&(top->virtualIPLock));

	/* give them the default cluster bandwidths if they asked */
	if(bwUpOut) {
		*bwUpOut = cluster_getBandwidthUp(cluster);
	}
	if(bwDownOut) {
		*bwDownOut = cluster_getBandwidthDown(cluster);
	}

	gchar* poiIPStr = address_ipToNewString(poiIP);
	info("connected address '%s' to PoI '%s' and cluster '%s'",
			address_toHostIPString(address), poiIPStr, cluster_getGeoCode(cluster));
	g_free(poiIPStr);
}

void topology_disconnect(Topology* top, Address* address) {
	MAGIC_ASSERT(top);
	in_addr_t ip = address_toNetworkIP(address);

	g_rw_lock_writer_lock(&(top->virtualIPLock));
	g_hash_table_remove(top->virtualIPToPoIIP, GUINT_TO_POINTER(ip));
	g_rw_lock_writer_unlock(&(top->virtualIPLock));
}

void topology_free(Topology* top) {
	MAGIC_ASSERT(top);

	g_mutex_lock(&top->graphLock);

	if(top->graphPath) {
		g_string_free(top->graphPath, TRUE);
	}

	g_rw_lock_writer_lock(&(top->virtualIPLock));

	/* this functions grabs and releases the pathCache write lock */
	_topology_clearCache(top);
	g_rw_lock_clear(&(top->pathCacheLock));

	if(top->currentEdgeWeights) {
		igraph_vector_destroy(top->currentEdgeWeights);
		g_free(top->currentEdgeWeights);
	}
	top->currentEdgeWeights = NULL;

	g_hash_table_destroy(top->virtualIPToPoIIP);
	top->virtualIPToPoIIP = NULL;
	g_hash_table_destroy(top->virtualIPToCluster);
	top->virtualIPToCluster = NULL;
	g_hash_table_destroy(top->poiIPToVertexIndex);
	top->poiIPToVertexIndex = NULL;
	g_hash_table_destroy(top->geocodeToCluster);
	top->geocodeToCluster = NULL;

	g_rw_lock_writer_unlock(&(top->virtualIPLock));
	g_rw_lock_clear(&(top->virtualIPLock));

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
	top->poiIPToVertexIndex = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
	top->virtualIPToPoIIP = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
	top->geocodeToCluster = g_hash_table_new_full(g_str_hash, g_str_equal,
			g_free, (GDestroyNotify) cluster_unref);
	top->virtualIPToCluster = g_hash_table_new_full(g_direct_hash, g_direct_equal,
			NULL, (GDestroyNotify) cluster_unref);

	/* first read in the graph and make sure its formed correctly,
	 * then setup our edge weights for shortest path */
	if(!_topology_loadGraph(top) || !_topology_checkGraph(top) || !_topology_setupGraph(top)) {
		topology_free(top);
		return NULL;
	}

	g_rw_lock_init(&(top->virtualIPLock));
	g_rw_lock_init(&(top->pathCacheLock));
	g_mutex_init(&(top->graphLock));

	return top;
}
