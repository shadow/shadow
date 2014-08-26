/*
 * See LICENSE for licensing information
 */

#include <igraph/igraph.h>

#include "shd-tgen.h"

struct _TGenGraph {
	gchar* path;
	igraph_t* graph;

	/* graph properties */
	igraph_integer_t clusterCount;
	igraph_integer_t vertexCount;
	igraph_integer_t edgeCount;
	igraph_bool_t isConnected;
	igraph_bool_t isDirected;
	igraph_bool_t isComplete;

	GHashTable* actions;

	gboolean hasStartAction;
	igraph_integer_t startActionVertexIndex;

	guint magic;
};

static gchar* _tgengraph_getHomePath(const gchar* path) {
	g_assert(path);
	GString* sbuffer = g_string_new(path);
	if(g_ascii_strncasecmp(path, "~", 1) == 0) {
		/* replace ~ with home directory */
		const gchar* home = g_get_home_dir();
		g_string_erase(sbuffer, 0, 1);
		g_string_prepend(sbuffer, home);
	}
	return g_string_free(sbuffer, FALSE);
}

static GError* _tgengraph_parseGraphEdges(TGenGraph* g) {
	TGEN_ASSERT(g);

	tgen_debug("checking graph edges...");

	/* we will iterate through the edges */
	igraph_eit_t edgeIterator;

	gint result = igraph_eit_create(g->graph, igraph_ess_all(IGRAPH_EDGEORDER_ID), &edgeIterator);
	if(result != IGRAPH_SUCCESS) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
				"igraph_eit_create return non-success code %i", result);
	}

	/* count the edges as we iterate */
	igraph_integer_t edgeCount = 0;

	while (!IGRAPH_EIT_END(edgeIterator)) {
		long int edgeIndex = IGRAPH_EIT_GET(edgeIterator);

		igraph_integer_t fromVertexIndex, toVertexIndex;

		gint result = igraph_edge(g->graph, edgeIndex, &fromVertexIndex, &toVertexIndex);
		if(result != IGRAPH_SUCCESS) {
			return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
					"igraph_edge return non-success code %i", result);
		}

		const gchar* fromIDStr = VAS(g->graph, "id", fromVertexIndex);
		const gchar* toIDStr = VAS(g->graph, "id", toVertexIndex);

		tgen_debug("found edge %li from vertex %li (%s) to vertex %li (%s)",
				(glong)edgeIndex, (glong)fromVertexIndex, fromIDStr, (glong)toVertexIndex, toIDStr);

		edgeCount++;
		IGRAPH_EIT_NEXT(edgeIterator);
	}

	igraph_eit_destroy(&edgeIterator);

	g->edgeCount = igraph_ecount(g->graph);
	if(g->edgeCount != edgeCount) {
		tgen_warning("igraph_vcount %f does not match iterator count %f", g->edgeCount, edgeCount);
	}

	tgen_info("%u graph edges ok", (guint) g->edgeCount);

	return NULL;
}

static void _tgengraph_storeAction(TGenGraph* g, TGenAction* a, igraph_integer_t vertexIndex) {
	TGEN_ASSERT(g);
	tgenaction_setKey(a, GINT_TO_POINTER(vertexIndex));
	g_hash_table_insert(g->actions, tgenaction_getKey(a), a);
}

static TGenAction* _tgengraph_getAction(TGenGraph* g, igraph_integer_t vertexIndex) {
	TGEN_ASSERT(g);
	return g_hash_table_lookup(g->actions, GINT_TO_POINTER(vertexIndex));
}

static GError* _tgengraph_parseStartVertex(TGenGraph* g, const gchar* idStr,
		igraph_integer_t vertexIndex) {
	TGEN_ASSERT(g);

	const gchar* timeStr = VAS(g->graph, "time", vertexIndex);
	const gchar* serverPortStr = VAS(g->graph, "serverport", vertexIndex);
	const gchar* peersStr = VAS(g->graph, "peers", vertexIndex);
	const gchar* socksProxyStr = VAS(g->graph, "socksproxy", vertexIndex);

	tgen_debug("validating action '%s' at vertex %li, time=%s serverport=%s socksproxy=%s peers=%s",
			idStr, (glong)vertexIndex, timeStr, serverPortStr, socksProxyStr, peersStr);

	if(g->hasStartAction) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
				"only one start vertex is allowed in the action graph");
	}
	// XXX TODO make sure start vertices do not contain self-loops

	GError* error = NULL;
	TGenAction* a = tgenaction_newStartAction(timeStr, serverPortStr, peersStr, socksProxyStr, &error);

	if(a) {
		_tgengraph_storeAction(g, a, vertexIndex);
		g_assert(!g->hasStartAction);
		g->startActionVertexIndex = vertexIndex;
		g->hasStartAction = TRUE;
	}

	return error;
}

static GError* _tgengraph_parseEndVertex(TGenGraph* g, const gchar* idStr,
		igraph_integer_t vertexIndex) {
	TGEN_ASSERT(g);

	/* the following termination conditions are optional */
	const gchar* timeStr = VAS(g->graph, "time", vertexIndex);
	const gchar* countStr = VAS(g->graph, "count", vertexIndex);
	const gchar* sizeStr = VAS(g->graph, "size", vertexIndex);

	tgen_debug("found vertex %li (%s), time=%s count=%s size=%s",
			(glong)vertexIndex, idStr, timeStr, countStr, sizeStr);

	GError* error = NULL;
	TGenAction* a = tgenaction_newEndAction(timeStr, countStr, sizeStr, &error);

	if(a) {
		_tgengraph_storeAction(g, a, vertexIndex);
	}

	return error;
}

static GError* _tgengraph_parsePauseVertex(TGenGraph* g, const gchar* idStr,
		igraph_integer_t vertexIndex) {
	TGEN_ASSERT(g);

	const gchar* timeStr = VAS(g->graph, "time", vertexIndex);

	tgen_debug("found vertex %li (%s), time=%s", (glong)vertexIndex, idStr, timeStr);

	GError* error = NULL;
	TGenAction* a = tgenaction_newPauseAction(timeStr, &error);

	if(a) {
		_tgengraph_storeAction(g, a, vertexIndex);
	}

	return error;
}

static GError* _tgengraph_parseSynchronizeVertex(TGenGraph* g, const gchar* idStr,
		igraph_integer_t vertexIndex) {
	TGEN_ASSERT(g);

	tgen_debug("found vertex %li (%s)", (glong)vertexIndex, idStr);

	GError* error = NULL;
	TGenAction* a = tgenaction_newSynchronizeAction(&error);

	if(a) {
		_tgengraph_storeAction(g, a, vertexIndex);
	}

	return error;
}

static GError* _tgengraph_parseTransferVertex(TGenGraph* g, const gchar* idStr,
		igraph_integer_t vertexIndex) {
	TGEN_ASSERT(g);

	const gchar* typeStr = VAS(g->graph, "type", vertexIndex);
	const gchar* protocolStr = VAS(g->graph, "protocol", vertexIndex);
	const gchar* sizeStr = VAS(g->graph, "size", vertexIndex);
	const gchar* peersStr = VAS(g->graph, "peers", vertexIndex);

	tgen_debug("found vertex %li (%s), type=%s protocol=%s size=%s peers=%s",
			(glong)vertexIndex, idStr, typeStr, protocolStr, sizeStr, peersStr);

	GError* error = NULL;
	TGenAction* a = tgenaction_newTransferAction(typeStr, protocolStr, sizeStr, peersStr, &error);

	if(a) {
		_tgengraph_storeAction(g, a, vertexIndex);
	}

	return error;
}

static GError* _tgengraph_parseGraphVertices(TGenGraph* g) {
	TGEN_ASSERT(g);

	tgen_debug("checking graph vertices...");

	/* we will iterate through the vertices */
	igraph_vit_t vertexIterator;

	gint result = igraph_vit_create(g->graph, igraph_vss_all(), &vertexIterator);
	if(result != IGRAPH_SUCCESS) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
				"igraph_vit_create return non-success code %i", result);
	}

	/* count the vertices as we iterate */
	igraph_integer_t vertexCount = 0;
	GError* error = NULL;

	while (!IGRAPH_VIT_END(vertexIterator)) {
		igraph_integer_t vertexIndex = (igraph_integer_t)IGRAPH_VIT_GET(vertexIterator);

		/* get vertex attributes: S for string and N for numeric */
		const gchar* idStr = VAS(g->graph, "id", vertexIndex);

		if(g_strstr_len(idStr, (gssize)-1, "start")) {
			error = _tgengraph_parseStartVertex(g, idStr, vertexIndex);
		} else if(g_strstr_len(idStr, (gssize)-1, "end")) {
			error = _tgengraph_parseEndVertex(g, idStr, vertexIndex);
		} else if(g_strstr_len(idStr, (gssize)-1, "pause")) {
			error = _tgengraph_parsePauseVertex(g, idStr, vertexIndex);
		} else if(g_strstr_len(idStr, (gssize)-1, "synchronize")) {
			error = _tgengraph_parseSynchronizeVertex(g, idStr, vertexIndex);
		} else if(g_strstr_len(idStr, (gssize)-1, "transfer")) {
			error = _tgengraph_parseTransferVertex(g, idStr, vertexIndex);
		} else {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
					"found vertex %li (%s) with an unknown action id '%s'",
					(glong)vertexIndex, idStr, idStr);
		}

		if(error) {
			break;
		}

		vertexCount++;
		IGRAPH_VIT_NEXT(vertexIterator);
	}

	/* clean up */
	igraph_vit_destroy(&vertexIterator);

	if(!error) {
		g->vertexCount = igraph_vcount(g->graph);
		if(g->vertexCount != vertexCount) {
			tgen_warning("igraph_vcount %f does not match iterator count %f", g->vertexCount, vertexCount);
		}

		tgen_info("%u graph vertices ok", (guint) g->vertexCount);
	}

	return error;
}

static GError* _tgengraph_parseGraphProperties(TGenGraph* g) {
	TGEN_ASSERT(g);
	gint result = 0;

	tgen_debug("checking graph properties...");

	/* IGRAPH_WEAK means the undirected version of the graph is connected
	 * IGRAPH_STRONG means a vertex can reach all others via a directed path */
	result = igraph_is_connected(g->graph, &(g->isConnected), IGRAPH_WEAK);
	if(result != IGRAPH_SUCCESS) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
				"igraph_is_connected return non-success code %i", result);
	}

	igraph_integer_t clusterCount;
	result = igraph_clusters(g->graph, NULL, NULL, &(g->clusterCount), IGRAPH_WEAK);
	if(result != IGRAPH_SUCCESS) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
				"igraph_clusters return non-success code %i", result);
	}

	/* it must be connected */
	if(!g->isConnected || g->clusterCount > 1) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
				"graph must be but is not connected");
	}

	g->isDirected = igraph_is_directed(g->graph);

	/* the topology is complete if the largest clique includes all vertices */
	igraph_integer_t largestCliqueSize = 0;
	result = igraph_clique_number(g->graph, &largestCliqueSize);
	if(result != IGRAPH_SUCCESS) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
				"igraph_clique_number return non-success code %i", result);
	}

	if(largestCliqueSize == igraph_vcount(g->graph)) {
		g->isComplete = (igraph_bool_t)TRUE;
	}

	tgen_debug("checking graph attributes...");

	/* now check list of all attributes */
	igraph_strvector_t gnames, vnames, enames;
	igraph_vector_t gtypes, vtypes, etypes;
	igraph_strvector_init(&gnames, 1);
	igraph_vector_init(&gtypes, 1);
	igraph_strvector_init(&vnames, igraph_vcount(g->graph));
	igraph_vector_init(&vtypes, igraph_vcount(g->graph));
	igraph_strvector_init(&enames, igraph_ecount(g->graph));
	igraph_vector_init(&etypes, igraph_ecount(g->graph));

	result = igraph_cattribute_list(g->graph, &gnames, &gtypes, &vnames, &vtypes, &enames, &etypes);
	if(result != IGRAPH_SUCCESS) {
		return g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
				"igraph_cattribute_list return non-success code %i", result);
	}

	gint i = 0;
	for(i = 0; i < igraph_strvector_size(&gnames); i++) {
		gchar* name = NULL;
		igraph_strvector_get(&gnames, (glong) i, &name);
		tgen_debug("found graph attribute '%s'", name);
	}
	for(i = 0; i < igraph_strvector_size(&vnames); i++) {
		gchar* name = NULL;
		igraph_strvector_get(&vnames, (glong) i, &name);
		tgen_debug("found vertex attribute '%s'", name);
	}
	for(i = 0; i < igraph_strvector_size(&enames); i++) {
		gchar* name = NULL;
		igraph_strvector_get(&enames, (glong) i, &name);
		tgen_debug("found edge attribute '%s'", name);
	}

	tgen_info("successfully verified graph properties and attributes");

	return NULL;
}

static igraph_t* _tgengraph_loadNewGraph(const gchar* path) {
	/* get the file */
	FILE* graphFile = fopen(path, "r");
	if(!graphFile) {
		tgen_critical("fopen returned NULL, problem opening graph file path '%s'", path);
		return FALSE;
	}

	tgen_info("reading graphml action graph at '%s'...", path);

	igraph_t* graph = g_new0(igraph_t, 1);
	gint result = igraph_read_graph_graphml(graph, graphFile, 0);
	fclose(graphFile);

	if(result != IGRAPH_SUCCESS) {
		tgen_critical("igraph_read_graph_graphml return non-success code %i", result);
		g_free(graph);
		return NULL;
	}

	tgen_info("successfully read graphml action graph at '%s'", path);

	return graph;
}

void tgengraph_free(TGenGraph* g) {
	TGEN_ASSERT(g);

	if(g->path) {
		g_free(g->path);
	}
	if(g->actions) {
		g_hash_table_destroy(g->actions);
	}
	if(g->graph) {
		igraph_destroy(g->graph);
		g_free(g->graph);
	}

	g_free(g);
}

TGenGraph* tgengraph_new(gchar* path) {
	TGenGraph* g = g_new0(TGenGraph, 1);
	g->magic = TGEN_MAGIC;

	g->actions = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)tgenaction_free);

	// TODO - if not path, write a temp file that contains a default test
	// graph that tests all possible protocols, etc, and use that as path instead of NULL
	// or better yet build one on the fly using the igraph api
	g->path = path ? _tgengraph_getHomePath(path) : NULL;

	GError* error = NULL;

	if(g->path) {
		/* use the built-in C attribute handler */
		igraph_attribute_table_t* oldHandler = igraph_i_set_attribute_table(&igraph_cattribute_table);

		g->graph = _tgengraph_loadNewGraph(g->path);
		if(!g->graph) {
			error = g_error_new(G_MARKUP_ERROR, G_MARKUP_ERROR_PARSE,
									"unable to read graph at path '%s'", g->path);
		}

		if(!error) {
			error = _tgengraph_parseGraphProperties(g);
		}
		if(!error) {
			error = _tgengraph_parseGraphVertices(g);
		}
		if(!error) {
			error = _tgengraph_parseGraphEdges(g);
		}

		/* replace the old handler */
		igraph_i_set_attribute_table(oldHandler);
	}

	if(error) {
		tgen_critical("error (%i) while loading graph: %s", error->code, error->message);
		g_error_free(error);
		tgengraph_free(g);
		return NULL;
	}

	tgen_message("successfully loaded graphml and validated actions: "
			"graph is %s with %u %s, %u %s, and %u %s",
			g->isConnected ? "weakly connected" : "disconnected",
			(guint)g->clusterCount, g->clusterCount == 1 ? "cluster" : "clusters",
			(guint)g->vertexCount, g->vertexCount == 1 ? "vertex" : "vertices",
			(guint)g->edgeCount, g->edgeCount == 1 ? "edge" : "edges");

	return g;
}

TGenAction* tgengraph_getStartAction(TGenGraph* g) {
	TGEN_ASSERT(g);
	return _tgengraph_getAction(g, g->startActionVertexIndex);
}

GQueue* tgengraph_getNextActions(TGenGraph* g, TGenAction* action) {
	TGEN_ASSERT(g);

	/* given an action, get all of the next actions in the dependency graph */

	gpointer key = tgenaction_getKey(action);
	igraph_integer_t srcVertexIndex = (igraph_integer_t) GPOINTER_TO_INT(key);

	/* initialize a vector to hold the result neighbor vertices for this action */
	igraph_vector_t* resultNeighborVertices = g_new0(igraph_vector_t, 1);

	/* initialize with 0 entries, since we dont know how many neighbors we have */
	int result = igraph_vector_init(resultNeighborVertices, 0);
	if(result != IGRAPH_SUCCESS) {
		tgen_critical("igraph_vector_init return non-success code %i", result);
		g_free(resultNeighborVertices);
		return FALSE;
	}

	/* initialize our result vector ptr to hold the vector of our 1 source */
	igraph_vector_ptr_t hoodVector;
	result = igraph_vector_ptr_init(&hoodVector, (long int) 1);
	if(result != IGRAPH_SUCCESS) {
		tgen_critical("igraph_vector_ptr_init return non-success code %i", result);
		igraph_vector_destroy(resultNeighborVertices);
		g_free(resultNeighborVertices);
		return NULL;
	}

	/* assign our single vertex vector to the result vector */
	igraph_vector_ptr_set(&hoodVector, (long int) 0, resultNeighborVertices);
	g_assert(resultNeighborVertices == igraph_vector_ptr_e(&hoodVector, (long int) 0));

	/* now get all outgoing 1-hop neighbors of the given action */
	result = igraph_neighborhood(g->graph, &hoodVector, igraph_vss_1(srcVertexIndex),
			(igraph_integer_t) 1, IGRAPH_OUT);
	if(result != IGRAPH_SUCCESS) {
		tgen_critical("igraph_neighborhood return non-success code %i", result);
		igraph_vector_ptr_destroy(&hoodVector);
		igraph_vector_destroy(resultNeighborVertices);
		g_free(resultNeighborVertices);
		return NULL;
	}

	/* handle the results */
	g_assert(resultNeighborVertices == igraph_vector_ptr_e(&hoodVector, (long int) 0));
	glong nVertices = igraph_vector_size(resultNeighborVertices);
	GQueue* nextActions = g_queue_new();

	for (gint i = 0; i < nVertices; i++) {
		igraph_integer_t dstVertexIndex = igraph_vector_e(resultNeighborVertices, i);
		TGenAction* nextAction = _tgengraph_getAction(g, dstVertexIndex);
		if(nextAction) {
			g_queue_push_tail(nextActions, nextAction);
		}
	}

	/* cleanup */
	igraph_vector_ptr_destroy(&hoodVector);
	igraph_vector_destroy(resultNeighborVertices);
	g_free(resultNeighborVertices);

	return nextActions;
}
