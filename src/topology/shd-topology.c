/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

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

	/* the edge weights currently used when computing shortest paths */
	igraph_vector_t* currentEdgeWeights;

	GHashTable* geocodeToVertex; // so we know where to place hosts
	GHashTable* addressToPoI; // so we know how to get latency

	/* cached latencies to avoid excessive shortest path lookups
	 * fromAddress->toAddress->latencyDouble*/
	GHashTable* latencyCache;
	MAGIC_DECLARE;
};

typedef void (*EdgeIteratorFunc)(Topology* top, long int edgeIndex);
typedef void (*VertexIteratorFunc)(Topology* top, long int vertexIndex);

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
	return TRUE;
}

static void _topology_checkGraphVerticesHelperHook(Topology* top, long int vertexIndex) {
	MAGIC_ASSERT(top);

	/* get vertex attributes: S for string and N for numeric */
	const gchar* idStr = VAS(&top->graph, "id", vertexIndex);
	const gchar* nodeIDStr = VAS(&top->graph, "nodeid", vertexIndex);
	const gchar* nodeTypeStr = VAS(&top->graph, "nodetype", vertexIndex);
	const gchar* geocodesStr = VAS(&top->graph, "geocodes", vertexIndex);
	gdouble asNumber = VAN(&top->graph, "asn", vertexIndex);

	debug("found vertex %li (%s), nodeid=%s nodetype=%s geocodes=%s asn=%i",
			vertexIndex, idStr, nodeIDStr, nodeTypeStr, geocodesStr, asNumber);
}

static igraph_integer_t _topology_iterateAllVertices(Topology* top, VertexIteratorFunc hook) {
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

		/* call the hook function for each edge */
		hook(top, vertexIndex);

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

static void _topology_checkGraphEdgesHelperHook(Topology* top, long int edgeIndex) {
	MAGIC_ASSERT(top);

	long int fromVertexIndex = IGRAPH_FROM(&top->graph, edgeIndex);
	const gchar* fromIDStr = VAS(&top->graph, "id", fromVertexIndex);

	long int toVertexIndex = IGRAPH_TO(&top->graph, edgeIndex);
	const gchar* toIDStr = VAS(&top->graph, "id", toVertexIndex);

	/* get edge attributes: S for string and N for numeric */
	const gchar* latenciesStr = EAS(&top->graph, "latencies", edgeIndex);

	debug("found edge %li from vertex %li (%s) to vertex %li (%s) latencies=%s",
			edgeIndex, fromVertexIndex, fromIDStr, toVertexIndex, toIDStr, latenciesStr);
}

static igraph_integer_t _topology_iterateAllEdges(Topology* top, EdgeIteratorFunc hook) {
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
		hook(top, edgeIndex);

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

static void _topology_updateEdgeWeightsHelperHook(Topology* top, long int edgeIndex) {
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

	return TRUE;
}

void topology_free(Topology* top) {
	MAGIC_ASSERT(top);

	if(top->graphPath) {
		g_string_free(top->graphPath, TRUE);
	}

	if(top->currentEdgeWeights) {
		igraph_vector_destroy(top->currentEdgeWeights);
		g_free(top->currentEdgeWeights);
	}

	MAGIC_CLEAR(top);
	g_free(top);
}

Topology* topology_new(gchar* graphPath) {
	g_assert(graphPath);
	Topology* top = g_new0(Topology, 1);
	MAGIC_INIT(top);

	top->graphPath = g_string_new(graphPath);

	/* first read in the graph and make sure its formed correctly,
	 * then setup our edge weights for shortest path */
	if(!_topology_loadGraph(top) || !_topology_checkGraph(top) ||
			!_topology_updateEdgeWeights(top)) {
		topology_free(top);
		return NULL;
	}

	return top;
}

//gboolean topology_connect(Address* hostAddress) {
//	// connect this address to a PoI and register it in our hashtable
//}
//
//void topology_disconnect(Address* hostAddress) {
//	address_unref(hostAddress);
//}
//
//static gdouble _topology_getCachedLatency(Topology* top, Address* source, Address* destination) {
//
//}
//
//static void _topology_setCachedLatency(Topology* top, Address* source, Address* destination) {
//
//}

gdouble topology_getLatency(Topology* top, Address* srcAddress, Address* dstAddress) {
	MAGIC_ASSERT(top);

//	gdouble cachedLatency = _topology_getCachedLatency(top, srcAddress, dstAddress);
//	if(cachedLatency > 0) {
//		return cachedLatency;
//	}

	igraph_integer_t srcVertexIndex;
	igraph_integer_t dstVertexIndex;
	igraph_vs_t dstVertexSet;
	gint result = igraph_vs_1(&dstVertexSet, dstVertexIndex);

//	igraph_vector_t edge_weights;
//	igraph_vector_init(&edge_weights, 0);
//	igraph_es_t all_edges;
//	igraph_es_all(&all_edges, IGRAPH_EDGEORDER_ID);
//	igraph_cattribute_EANV(&graph, "latency", all_edges, &edge_weights);

	igraph_vector_ptr_t pathEdgesLists;
	igraph_vector_ptr_init(&pathEdgesLists, 0);

	igraph_get_shortest_paths_dijkstra(&top->graph, &pathEdgesLists, srcVertexIndex,
			dstVertexSet, top->currentEdgeWeights, IGRAPH_OUT);

	// TODO iterate the pathEdges vector and sum the latencies
    for (gint i = 0; i < igraph_vector_ptr_size(&pathEdgesLists); i++) {
      igraph_vector_t* pathEdges = (igraph_vector_t*) igraph_vector_ptr_e(&pathEdgesLists, i);

//      process_computed_edge(&graph, edge_list, v2_vid);

      igraph_vector_clear(pathEdges);
    }

	igraph_vector_ptr_destroy(&pathEdgesLists);

	return G_MAXDOUBLE;
}
