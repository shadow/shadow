/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Topology {
	GString* graphPath;
	igraph_t graph;

	igraph_integer_t clusterCount;
	igraph_integer_t vertexCount;
	igraph_integer_t edgeCount;
	igraph_bool_t isConnected;

//	igraph_vector_t currentEdgeWeights;
//	igraph_es_t allEdges;
//	igraph_vs_t allVertices;
	MAGIC_DECLARE;
};

static gboolean _topology_loadGraph(Topology* top) {
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
	gint result = 0;

	/* check some graph properties */
	result = igraph_is_connected(&top->graph, &(top->isConnected), IGRAPH_WEAK);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_is_connected return non-success code %i", result);
		return FALSE;
	}

	igraph_integer_t clusterCount;
	result = igraph_clusters(&top->graph, NULL, NULL, &(top->clusterCount), IGRAPH_WEAK);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_clusters return non-success code %i", result);
		return FALSE;
	}

	/* it must be connected */
	if(!top->isConnected || top->clusterCount > 1) {
		critical("topology must be but is not fully connected", top->graphPath->str);
		return FALSE;
	}

	info("topology is %sconnected with %u clusters", top->isConnected ? "" : "dis", (guint)top->clusterCount);
	return TRUE;
}

static gboolean _topology_checkGraphVertices(Topology* top) {
	gint result = 0;

	info("checking graph vertices");

	/* initialize our vertex set to select from all vertices in the graph */
	igraph_vs_t allVertices;
	result = igraph_vs_all(&allVertices);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vs_all return non-success code %i", result);
		return FALSE;
	}

	/* we will iterate through the vertices */
	igraph_vit_t vertexIterator;
	result = igraph_vit_create(&top->graph, allVertices, &vertexIterator);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_vit_create return non-success code %i", result);
		return FALSE;
	}

	/* count the vertices as we iterate */
	igraph_integer_t vertexCount = 0;
	while (!IGRAPH_VIT_END(vertexIterator)) {
		long int vertexID = IGRAPH_VIT_GET(vertexIterator);

		/* get vertex attributes: S for string and N for numeric */
		const gchar* nodeIDStr = VAS(&top->graph, "nodeid", vertexID);
		const gchar* nodeTypeStr = VAS(&top->graph, "nodetype", vertexID);
		const gchar* geocodesStr = VAS(&top->graph, "geocodes", vertexID);
		gdouble asNumber = VAN(&top->graph, "asn", vertexID);

		debug("found vertex %li nodeid=%s nodetype=%s geocodes=%s asn=%i",
				vertexID, nodeIDStr, nodeTypeStr, geocodesStr, asNumber);

		vertexCount++;
		IGRAPH_VIT_NEXT(vertexIterator);
	}

	/* clean up */
	igraph_vit_destroy(&vertexIterator);
	igraph_vs_destroy(&allVertices);

	top->vertexCount = igraph_vcount(&top->graph);

	if(top->vertexCount != vertexCount) {
		warning("igraph_vcount does not match iterator count");
	}

	info("%u graph vertices ok", (guint) top->vertexCount);

	return TRUE;
}

static gboolean _topology_checkGraphEdges(Topology* top) {
	gint result = 0;

	igraph_es_t allEdges;
	result = igraph_es_all(&allEdges, IGRAPH_EDGEORDER_ID);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_es_all return non-success code %i", result);
		return FALSE;
	}

	/* we will iterate through the edges */
	igraph_eit_t edgeIterator;
	result = igraph_eit_create(&top->graph, allEdges, &edgeIterator);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_eit_create return non-success code %i", result);
		return FALSE;
	}

	/* count the edges as we iterate */
	igraph_integer_t edgeCount = 0;
	while (!IGRAPH_EIT_END(edgeIterator)) {
		long int edgeID = IGRAPH_EIT_GET(edgeIterator);
		long int fromVertexID = IGRAPH_FROM(&top->graph, edgeID);
		long int toVertexID = IGRAPH_TO(&top->graph, edgeID);

		/* get vertex attributes: S for string and N for numeric */
		const gchar* latenciesStr = EAS(&top->graph, "latencies", edgeID);

		debug("found edge %li from vertex %li to vertex %li latencies=%s",
				edgeID, fromVertexID, toVertexID, latenciesStr);

		edgeCount++;
		IGRAPH_EIT_NEXT(edgeIterator);
	}

	igraph_eit_destroy(&edgeIterator);
	igraph_es_destroy(&allEdges);

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
			top->graphPath->str, top->isConnected ? "connected" : "disconnected",
			(guint)top->clusterCount, top->clusterCount == 1 ? "cluster" : "clusters",
			(guint)top->vertexCount, top->vertexCount == 1 ? "vertex" : "vertices",
			(guint)top->edgeCount, top->edgeCount == 1 ? "edge" : "edges");

	return TRUE;
}

Topology* topology_new(gchar* graphPath) {
	g_assert(graphPath);
	Topology* top = g_new0(Topology, 1);
	MAGIC_INIT(top);

	top->graphPath = g_string_new(graphPath);

	/* first read in the graph and make sure its formed correctly */
	if(!_topology_loadGraph(top) || !_topology_checkGraph(top)) {
		g_string_free(top->graphPath, TRUE);
		return NULL;
	}

	/* setup our current weights for shortest path */
//	igraph_vector_init(&top->currentEdgeWeights, 0);
//	result = igraph_cattribute_EANV(&top->graph, "latency", top->allEdges,
//			top->currentEdgeWeights);

	return top;
}

void topology_free(Topology* top) {
	MAGIC_ASSERT(top);

	if(top->graphPath) {
		g_string_free(top->graphPath, TRUE);
	}

//	igraph_vs_destroy(&top->allVertices);
//	igraph_es_destroy(&top->allEdges);

	MAGIC_CLEAR(top);
	g_free(top);
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
//
//gdouble topology_getLatency(Topology* top, Address* srcAddress, Address* dstAddress) {
//	MAGIC_ASSERT(top);
//
//	PoI* srcPoI;
//	PoI* dstPoI;
//
//	gdouble cachedLatency = _topology_getCachedLatency(top, srcAddress, dstAddress);
//	if(cachedLatency) {
//		return cachedLatency;
//	}
//
//	gboolean cached;
//	if(!cached) {
////		igraph_vs_t dstVertexSet;
////		igraph_integer_t dstVertexID;
////		gint result = igraph_vs_1(&dstVertexSet, dstVertexID);
////
////		igraph_get_shortest_paths_dijkstra(&top->graph, 0, edges, sourceVertexID,
////				possibleVertexDestinations, &edge_weights, IGRAPH_OUT);
//	}
//	return G_MAXDOUBLE;
//}
