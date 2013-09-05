/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Topology {
	igraph_t* graph;
	GString* path;
	igraph_integer_t clusterCount;
	igraph_integer_t vertexCount;
	igraph_integer_t edgeCount;
	igraph_bool_t isConnected;
	MAGIC_DECLARE;
};

Topology* topology_new(gchar* graphPath) {
	g_assert(graphPath);
	Topology* top = g_new0(Topology, 1);
	MAGIC_INIT(top);

	top->graph = g_new0(igraph_t, 1);
	top->path = g_string_new(graphPath);

	/* initialize the built-in C attribute handler */
	igraph_attribute_table_t* oldHandler = igraph_i_set_attribute_table(&igraph_cattribute_table);

	/* get the file */
	FILE* graphFile = fopen(top->path->str, "r");
	if(!graphFile) {
		critical("fopen returned NULL, problem opening graph file path '%s'", top->path->str);
		return NULL;
	}

	info("reading the topology graph at '%s'", top->path->str);

	int result = igraph_read_graph_graphml(top->graph, graphFile, 0);
	fclose(graphFile);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_read_graph_graphml return non-success code %i", result);
		return NULL;
	}

	/* check some graph properties */
	result = igraph_is_connected(top->graph, &(top->isConnected), IGRAPH_WEAK);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_is_connected return non-success code %i", result);
		return NULL;
	}

	igraph_integer_t clusterCount;
	result = igraph_clusters(top->graph, NULL, NULL, &(top->clusterCount), IGRAPH_WEAK);
	if(result != IGRAPH_SUCCESS) {
		critical("igraph_clusters return non-success code %i", result);
		return NULL;
	}

	top->vertexCount = igraph_vcount(top->graph);
	top->edgeCount = igraph_ecount(top->graph);

	message("successfully parsed graphml XML topology '%s': graph is %s with %u clusters, %u vertices, and %u edges",
			top->path->str, top->isConnected ? "connected" : "disconnected",
			(guint)top->clusterCount, (guint)top->vertexCount, (guint)top->edgeCount);

	if(!top->isConnected || top->clusterCount > 1) {
		critical("topology graph at '%s' is not fully connected", top->path->str);
		return NULL;
	}

	return top;
}

void topology_free(Topology* top) {
	MAGIC_ASSERT(top);

	if(top->path) {
		g_string_free(top->path, TRUE);
	}
	if(top->graph) {
		g_free(top->graph);
	}

	MAGIC_CLEAR(top);
	g_free(top);
}
