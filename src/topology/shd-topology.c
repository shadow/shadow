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

typedef struct _PathCacheEntry PathCacheEntry;
struct _PathCacheEntry {
	gdouble latency;
	gdouble reliablity;
};

typedef struct _GeoCluster GeoCluster;
struct _GeoCluster {
	GQueue* poiVertexIndicies;
	guint bandwidthUp;
	guint bandwidthDown;
	gdouble packetLoss;
};

struct _Topology {
	/* the file path of the graphml file */
	GString* graphPath;
	/* the imported igraph graph data */
	igraph_t graph;

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

	/* the edge weights currently used when computing shortest paths */
	igraph_vector_t* currentEdgeWeights;

	/* each connected address is assigned to a PoI vertex. we store the mapping
	 * so we can correctly lookup the assigned edge when computing latency. */
	GHashTable* ipToVertexIndex;

	/* stores the mapping between geocode and its cluster properties.
	 * this is useful when placing hosts without an IP in the topology */
	GHashTable* geocodeToGeoCluster;

	/* cached latencies to avoid excessive shortest path lookups
	 * store a cache table for every connected address
	 * fromAddress->toAddress->PathCacheEntry */
	GHashTable* pathCache;
	GMutex pathCacheLock; // FIXME use GRWLock

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
	g_assert(hook);

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
	g_assert(hook);

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
	gdouble randomDouble = engine_nextRandomDouble(worker_getPrivate()->cached_engine);
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

	if(g_ascii_strcasecmp(nodeTypeStr, "pop")) {
		/* this is a point of interest (poi) not a point of presence (pop) */
		in_addr_t hostIP = address_stringToIP(idStr);
		if(hostIP == INADDR_NONE) {
			error("graph topology error: points of interest (nodes that are not 'pop's) should have IP address as ID");
			return;
		}

		/* this is a PoI that we can connect hosts to, track its graph index */
		in_addr_t networkIP = htonl(hostIP);
		g_hash_table_replace(top->ipToVertexIndex, GUINT_TO_POINTER(networkIP), GINT_TO_POINTER(vertexIndex));

		/* add it to the list of vertices by its geoclusters, for assigning hosts randomly later */
		gchar** geocodeParts = g_strsplit(geocodesStr, ",", 0);
		for(gint i = 0; geocodeParts[i] != NULL; i++) {
			GeoCluster* cluster = g_hash_table_lookup(top->geocodeToGeoCluster, geocodeParts[i]);
			if(cluster) {
				g_queue_push_tail(cluster->poiVertexIndicies, GINT_TO_POINTER(vertexIndex));
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
		g_assert(geocode && value);

		GeoCluster* cluster = g_hash_table_lookup(top->geocodeToGeoCluster, geocode);
		if(!cluster) {
			cluster = g_new0(GeoCluster, 1);
			cluster->poiVertexIndicies = g_queue_new();
			g_hash_table_replace(top->geocodeToGeoCluster, g_strdup(geocode), cluster);
		}

		if(type == CA_PACKETLOSS) {
			cluster->packetLoss = (gdouble) atof(value);
		} else if (type == CA_BANDWIDTHUP) {
			cluster->bandwidthUp = (guint) atoi(value);
		} else if (type == CA_BANDWIDTHDOWN) {
			cluster->bandwidthDown = (guint) atoi(value);
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

	info("found %u geocode clusters", g_hash_table_size(top->geocodeToGeoCluster));
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
	if(top->pathCache) {
		g_hash_table_destroy(top->pathCache);
		top->pathCache = NULL;
	}
}

static PathCacheEntry* _topology_getPathFromCache(Topology* top, Address* source, Address* destination) {
	MAGIC_ASSERT(top);

	if(top->pathCache) {
		/* look for the source first level cache */
		ShadowID srcID = address_getID(source);
		gpointer sourceCache = g_hash_table_lookup(top->pathCache, GUINT_TO_POINTER(srcID));

		if(sourceCache) {
			/* check for the path to destination in source cache */
			ShadowID dstID = address_getID(destination);
			PathCacheEntry* entry = g_hash_table_lookup(sourceCache, GUINT_TO_POINTER(dstID));
			if(entry) {
				/* yay, cache hit! */
				return entry;
			}
		}
	}

	/* cache miss */
	return NULL;
}

static void _topology_storePathInCache(Topology* top, Address* source, Address* destination,
		igraph_real_t latency, igraph_real_t reliability) {
	MAGIC_ASSERT(top);

	ShadowID srcID = address_getID(source);
	ShadowID dstID = address_getID(destination);

	g_mutex_lock(&(top->pathCacheLock));

	/* create latency cache on the fly */
	if(!top->pathCache) {
		/* stores hash tables for source address caches */
		top->pathCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_hash_table_destroy);
	}

	GHashTable* sourceCache = g_hash_table_lookup(top->pathCache, GUINT_TO_POINTER(srcID));
	if(!sourceCache) {
		/* dont have a cache for this source yet, create one now
		 * use g_free to free the value since this table will store latency pointers */
		sourceCache = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
		g_hash_table_replace(top->pathCache, GUINT_TO_POINTER(srcID), sourceCache);
	}

	PathCacheEntry* entry = g_new0(PathCacheEntry, 1);
	entry->latency = (gdouble) latency;
	entry->reliablity = (gdouble) reliability;
	g_hash_table_replace(sourceCache, GUINT_TO_POINTER(dstID), entry);

	g_mutex_unlock(&(top->pathCacheLock));
}

static igraph_integer_t _topology_getConnectedVertexIndex(Topology* top, Address* address) {
	MAGIC_ASSERT(top);

	in_addr_t ip = address_toNetworkIP(address);
	igraph_integer_t* vertexIndex = g_hash_table_lookup(top->ipToVertexIndex, GUINT_TO_POINTER(ip));

	if(!vertexIndex) {
		warning("address %s is not connected to the topology", address_toString(address));
		return (igraph_integer_t) -1;
	}

	return *vertexIndex;
}

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
	g_assert(&resultPathVertices == igraph_vector_ptr_e(&resultPaths, 0));

	/* run dijkstra's shortest path algorithm */
#ifndef IGRAPH_VERSION
	result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths,
			srcVertexIndex, dstVertexSet, top->currentEdgeWeights, IGRAPH_OUT);
#else
	result = igraph_get_shortest_paths_dijkstra(&top->graph, &resultPaths, NULL,
			srcVertexIndex, dstVertexSet, top->currentEdgeWeights, IGRAPH_OUT);
#endif
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
	igraph_real_t totalLatency = 0, edgeLatency;
	igraph_real_t totalReliability = 1, edgeReliability;
	igraph_integer_t edgeIndex, fromVertexIndex, toVertexIndex;

	/* the first vertex is our starting point
	 * igraph_vector_size can be 0 for paths to ourself */
	if(igraph_vector_size(&resultPathVertices) > 0) {
		fromVertexIndex = VECTOR(resultPathVertices)[0];
		const gchar* fromIDStr = VAS(&top->graph, "id", fromVertexIndex);
		g_string_append_printf(pathString, "%s", fromIDStr);
		// TODO add reliability
	}

	/* iterate the edges in the path and sum the latencies */
	for (gint i = 1; i < igraph_vector_size(&resultPathVertices); i++) {
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

		// TODO add actual reliability
		edgeReliability = 1;
		totalReliability *= edgeReliability;

		/* accumulate path information */
		const gchar* toIDStr = VAS(&top->graph, "id", toVertexIndex);
		g_string_append_printf(pathString, "--[%f,%f]-->%s", edgeLatency, edgeReliability, toIDStr);

		/* update for next edge */
		fromVertexIndex = toVertexIndex;
	}

	debug("shortest path %s-->%s is %f ms %f loss: %s", srcIDStr, dstIDStr,
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
	PathCacheEntry* entry = _topology_getPathFromCache(top, srcAddress, dstAddress);
	if(!entry) {
		/* cache miss, compute the path using shortest latency path from src to dst */
		gboolean isSuccess = _topology_computePath(top, srcAddress, dstAddress);
		entry = _topology_getPathFromCache(top, srcAddress, dstAddress);
	}

	if(entry) {
		if(latency) {
			*latency = entry->latency;
		}
		if(reliability) {
			*reliability = entry->reliablity;
		}
		return TRUE;
	} else {
		/* some error computing or cacheing path */
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

gboolean topology_connect(Topology* top, Address* address, gchar* requestedCluster) {
	MAGIC_ASSERT(top);
	// TODO connect this address to a PoI and register it in our hashtable
	// ip may already be in the vertex table because it matches a poi
	// what if we have several hosts attached to a poi?
	// we may have to select based on geocode or longest prefix match
	//
	// if ip, use that, else if cluster, choose longest prefix matched poi from geocluster queue
	// else choose longest prefix match from all pois
	//
	// TODO return bandwidth up/down in case they never got assigned one
	return FALSE;
}

void topology_disconnect(Topology* top, Address* address) {
	MAGIC_ASSERT(top);
	// TODO
//	address_unref(hostAddress);
}

void topology_free(Topology* top) {
	MAGIC_ASSERT(top);

	g_mutex_lock(&(top->pathCacheLock));

	if(top->graphPath) {
		g_string_free(top->graphPath, TRUE);
	}

	if(top->currentEdgeWeights) {
		igraph_vector_destroy(top->currentEdgeWeights);
		g_free(top->currentEdgeWeights);
	}

	_topology_clearCache(top);
	g_hash_table_destroy(top->ipToVertexIndex);
	g_hash_table_destroy(top->geocodeToGeoCluster);

	g_mutex_unlock(&(top->pathCacheLock));
	g_mutex_clear(&(top->pathCacheLock));

	MAGIC_CLEAR(top);
	g_free(top);
}

Topology* topology_new(gchar* graphPath) {
	g_assert(graphPath);
	Topology* top = g_new0(Topology, 1);
	MAGIC_INIT(top);

	top->graphPath = g_string_new(graphPath);
	top->ipToVertexIndex = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
	top->geocodeToGeoCluster = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

	/* first read in the graph and make sure its formed correctly,
	 * then setup our edge weights for shortest path */
	if(!_topology_loadGraph(top) || !_topology_checkGraph(top) || !_topology_setupGraph(top)) {
		topology_free(top);
		return NULL;
	}

	g_mutex_init(&(top->pathCacheLock));

	return top;
}
