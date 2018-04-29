/*
 * See LICENSE for licensing information
 */

#include <igraph.h>

#include "shd-tgen.h"

typedef enum _VertexAttribute VertexAttribute;
enum _VertexAttribute {
    VERTEX_ATTR_ID=1,
    VERTEX_ATTR_TYPE=2,
};

typedef enum _EdgeAttribute EdgeAttribute;
enum _EdgeAttribute {
    EDGE_ATTR_TYPE=3,
    EDGE_ATTR_WEIGHT=4,
    EDGE_ATTR_LOGNORMMU=5,
    EDGE_ATTR_LOGNORMSIGMA=6,
    EDGE_ATTR_EXPLAMBDA=7,
};

typedef enum _VertexType VertexType;
enum _VertexType {
    VERTEX_TYPE_STATE=8,
    VERTEX_TYPE_OBSERVATION=9,
};

typedef enum _EdgeType EdgeType;
enum _EdgeType {
    EDGE_TYPE_TRANSITION=10,
    EDGE_TYPE_EMISSION=11,
};

typedef enum _VertexID VertexID;
enum _VertexID {
    VERTEX_ID_START=12,
    VERTEX_ID_PACKET_TO_SERVER=13,
    VERTEX_ID_PACKET_TO_ORIGIN=14,
    VERTEX_ID_STREAM=15,
    VERTEX_ID_END=16,
};

struct _TGenMarkovModel {
    gint refcount;

    igraph_t *graph;
    igraph_integer_t startVertexID;

    guint magic;
};

static const gchar* _tgenmarkovmodel_vertexAttributeToString(VertexAttribute attr) {
    if(attr == VERTEX_ATTR_ID) {
        return "id";
    } else if(attr == VERTEX_ATTR_TYPE) {
        return "type";
    } else {
        return "unknown";
    }
}

static const gchar* _tgenmarkovmodel_edgeAttributeToString(EdgeAttribute attr) {
    if(attr == EDGE_ATTR_TYPE) {
        return "type";
    } else if(attr == EDGE_ATTR_WEIGHT) {
        return "weight";
    } else if(attr == EDGE_ATTR_LOGNORMMU) {
        return "lognorm_mu";
    } else if(attr == EDGE_ATTR_LOGNORMSIGMA) {
        return "lognorm_sigma";
    } else if(attr == EDGE_ATTR_EXPLAMBDA) {
        return "exp_lambda";
    } else {
        return "unknown";
    }
}

static const gchar* _tgenmarkovmodel_vertexTypeToString(VertexType type) {
    if(type == VERTEX_TYPE_STATE) {
        return "state";
    } else if(type == VERTEX_TYPE_OBSERVATION) {
        return "observation";
    } else {
        return "unknown";
    }
}

static size_t _tgenmarkovmodel_vertexTypeStringLength(VertexType type) {
    return strlen(_tgenmarkovmodel_vertexTypeToString(type));
}

static gboolean _tgenmarkovmodel_vertexTypeIsEqual(const gchar* typeStr, VertexType type) {
    const gchar* constTypeStr = _tgenmarkovmodel_vertexTypeToString(type);
    size_t length = _tgenmarkovmodel_vertexTypeStringLength(type);
    return (g_ascii_strncasecmp(typeStr, constTypeStr, length) == 0) ? TRUE : FALSE;
}

static const gchar* _tgenmarkovmodel_edgeTypeToString(EdgeType type) {
    if(type == EDGE_TYPE_TRANSITION) {
        return "transition";
    } else if(type == EDGE_TYPE_EMISSION) {
        return "emission";
    } else {
        return "unknown";
    }
}

static size_t _tgenmarkovmodel_edgeTypeStringLength(EdgeType type) {
    return strlen(_tgenmarkovmodel_edgeTypeToString(type));
}

static gboolean _tgenmarkovmodel_edgeTypeIsEqual(const gchar* typeStr, EdgeType type) {
    const gchar* constTypeStr = _tgenmarkovmodel_edgeTypeToString(type);
    size_t length = _tgenmarkovmodel_edgeTypeStringLength(type);
    return (g_ascii_strncasecmp(typeStr, constTypeStr, length) == 0) ? TRUE : FALSE;
}

static const gchar* _tgenmarkovmodel_vertexIDToString(VertexID id) {
    if(id == VERTEX_ID_START) {
        return "start";
    } else if(id == VERTEX_ID_PACKET_TO_SERVER) {
        return "+";
    } else if(id == VERTEX_ID_PACKET_TO_ORIGIN) {
        return "-";
    } else if(id == VERTEX_ID_STREAM) {
        return "$";
    } else if(id == VERTEX_ID_END) {
        return "F";
    } else {
        return "?";
    }
}

static size_t _tgenmarkovmodel_vertexIDStringLength(VertexID id) {
    return strlen(_tgenmarkovmodel_vertexIDToString(id));
}

static gboolean _tgenmarkovmodel_vertexIDIsEqual(const gchar* idStr, VertexID id) {
    const gchar* constIDStr = _tgenmarkovmodel_vertexIDToString(id);
    size_t length = _tgenmarkovmodel_vertexIDStringLength(id);
    return (g_ascii_strncasecmp(idStr, constIDStr, length) == 0) ? TRUE : FALSE;
}

static gboolean _tgenmarkovmodel_vertexIDIsEmission(const gchar* idStr) {
    if(_tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_PACKET_TO_SERVER) ||
            _tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_PACKET_TO_ORIGIN) ||
            _tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_STREAM) ||
            _tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_END)) {
        return TRUE;
    } else {
        return FALSE;
    }
}

/* if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _tgenmarkovmodel_findVertexAttributeString(TGenMarkovModel* mmodel, igraph_integer_t vertexIndex,
        VertexAttribute attr, const gchar** valueOut) {
    TGEN_ASSERT(mmodel);

    const gchar* name = _tgenmarkovmodel_vertexAttributeToString(attr);

    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_VERTEX, name)) {
        const gchar* value = igraph_cattribute_VAS(mmodel->graph, name, vertexIndex);
        if(value != NULL && value[0] != '\0') {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _tgenmarkovmodel_findEdgeAttributeDouble(TGenMarkovModel* mmodel, igraph_integer_t edgeIndex,
        EdgeAttribute attr, gdouble* valueOut) {
    TGEN_ASSERT(mmodel);

    const gchar* name = _tgenmarkovmodel_edgeAttributeToString(attr);

    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, name)) {
        gdouble value = (gdouble) igraph_cattribute_EAN(mmodel->graph, name, edgeIndex);
        if(isnan(value) == 0) {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/* if the value is found and not NULL, it's value is returned in valueOut.
 * returns true if valueOut has been set, false otherwise */
static gboolean _tgenmarkovmodel_findEdgeAttributeString(TGenMarkovModel* mmodel, igraph_integer_t edgeIndex,
        EdgeAttribute attr, const gchar** valueOut) {
    TGEN_ASSERT(mmodel);

    const gchar* name = _tgenmarkovmodel_edgeAttributeToString(attr);

    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, name)) {
        const gchar* value = igraph_cattribute_EAS(mmodel->graph, name, edgeIndex);
        if(value != NULL && value[0] != '\0') {
            if(valueOut != NULL) {
                *valueOut = value;
                return TRUE;
            }
        }
    }

    return FALSE;
}

static gboolean _tgenmarkovmodel_checkVertexAttributes(TGenMarkovModel* mmodel, igraph_integer_t vertexIndex) {
    TGEN_ASSERT(mmodel);
    g_assert(mmodel->graph);

    gboolean isSuccess = TRUE;
    GString* message = g_string_new(NULL);
    g_string_printf(message, "found vertex %li", (glong)vertexIndex);

    /* keep a copy of the id once we get it to make the following message more understandable */
    gchar* idStr = NULL;

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* idKey = _tgenmarkovmodel_vertexAttributeToString(VERTEX_ATTR_ID);
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_VERTEX, idKey)) {
        const gchar* vidStr;
        if(_tgenmarkovmodel_findVertexAttributeString(mmodel, vertexIndex, VERTEX_ATTR_ID, &vidStr)) {
            g_string_append_printf(message, " %s='%s'", idKey, vidStr);
            idStr = g_strdup(vidStr);
        } else {
            tgen_warning("required attribute '%s' on vertex %li is NULL", idKey, (glong)vertexIndex);
            isSuccess = FALSE;
            idStr = g_strdup("NULL");
        }
    } else {
        tgen_warning("required attribute '%s' on vertex %li is missing", idKey, (glong)vertexIndex);
        isSuccess = FALSE;
        idStr = g_strdup("MISSING");
    }

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* typeKey = _tgenmarkovmodel_vertexAttributeToString(VERTEX_ATTR_TYPE);
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_VERTEX, typeKey)) {
        const gchar* typeStr;
        if(_tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_START)) {
            /* start vertex doesnt need any attributes */
        } else if(_tgenmarkovmodel_findVertexAttributeString(mmodel, vertexIndex, VERTEX_ATTR_TYPE, &typeStr)) {
            g_string_append_printf(message, " %s='%s'", typeKey, typeStr);

            if(_tgenmarkovmodel_vertexTypeIsEqual(typeStr, VERTEX_TYPE_STATE)) {
                /* pass, nothing to do for now */
            } else if(_tgenmarkovmodel_vertexTypeIsEqual(typeStr, VERTEX_TYPE_OBSERVATION)) {
                if(!_tgenmarkovmodel_vertexIDIsEmission(idStr)) {
                    tgen_warning("'$s' type on vertex %li must be one of '%s', '%s', '%s', or '%s', "
                            "but you gave %s='%s'",
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_TYPE_OBSERVATION),
                            (glong)vertexIndex,
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_ID_PACKET_TO_SERVER),
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_ID_PACKET_TO_ORIGIN),
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_ID_STREAM),
                            _tgenmarkovmodel_vertexTypeToString(VERTEX_ID_END),
                            idKey, idStr);
                    isSuccess = FALSE;
                }
            } else {
                tgen_warning("required attribute '%s' value '%s' on vertex %li is invalid, "
                        "need '%s' or '%s'",
                        typeKey, typeStr, (glong)vertexIndex,
                        _tgenmarkovmodel_vertexTypeToString(VERTEX_TYPE_STATE),
                        _tgenmarkovmodel_vertexTypeToString(VERTEX_TYPE_OBSERVATION));
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on vertex %li is NULL", typeKey, (glong)vertexIndex);
            isSuccess = FALSE;
        }
    } else {
        tgen_warning("required attribute '%s' on vertex %li is missing", typeKey, (glong)vertexIndex);
        isSuccess = FALSE;
    }

    tgen_debug("%s", message->str);

    g_string_free(message, TRUE);
    g_free(idStr);

    return isSuccess;
}

static gboolean _tgenmarkovmodel_validateVertices(TGenMarkovModel* mmodel, igraph_integer_t* startVertexID) {
    TGEN_ASSERT(mmodel);
    g_assert(mmodel->graph);

    gboolean isSuccess = TRUE;
    gboolean foundStart = FALSE;

    igraph_vit_t vertexIterator;
    gint result = igraph_vit_create(mmodel->graph, igraph_vss_all(), &vertexIterator);

    if (result != IGRAPH_SUCCESS) {
        isSuccess = FALSE;
        return isSuccess;
    }

    while (!IGRAPH_VIT_END(vertexIterator)) {
        igraph_integer_t vertexIndex = IGRAPH_VIT_GET(vertexIterator);

        isSuccess = _tgenmarkovmodel_checkVertexAttributes(mmodel, vertexIndex);

        if(!isSuccess) {
            break;
        }

        const gchar* idStr = VAS(mmodel->graph,
                _tgenmarkovmodel_vertexAttributeToString(VERTEX_ATTR_ID), vertexIndex);
        if (_tgenmarkovmodel_vertexIDIsEqual(idStr, VERTEX_ID_START)) {
            /* found the start vertex */
            foundStart = TRUE;

            if(startVertexID) {
                *startVertexID = vertexIndex;
            }
        }

        IGRAPH_VIT_NEXT(vertexIterator);
    }

    if(!foundStart) {
        tgen_warning("unable to find start id in markov model graph");
    }

    igraph_vit_destroy(&vertexIterator);
    return isSuccess && foundStart;
}

static gboolean _tgenmarkovmodel_checkEdgeAttributes(TGenMarkovModel* mmodel, igraph_integer_t edgeIndex) {
    TGEN_ASSERT(mmodel);
    g_assert(mmodel->graph);

    igraph_integer_t fromVertexIndex, toVertexIndex;
    gint result = igraph_edge(mmodel->graph, edgeIndex, &fromVertexIndex, &toVertexIndex);

    if(result != IGRAPH_SUCCESS) {
        tgen_warning("igraph_edge return non-success code %i", result);
        return FALSE;
    }

    gboolean found = FALSE;
    const gchar* fromIDStr = NULL;
    const gchar* toIDStr = NULL;

    found = _tgenmarkovmodel_findVertexAttributeString(mmodel, fromVertexIndex, VERTEX_ATTR_ID, &fromIDStr);
    if(!found) {
        tgen_warning("unable to find source vertex for edge %li", (glong)edgeIndex);
        return FALSE;
    }

    found = _tgenmarkovmodel_findVertexAttributeString(mmodel, toVertexIndex, VERTEX_ATTR_ID, &toIDStr);
    if(!found) {
        tgen_warning("unable to find destination vertex for edge %li", (glong)edgeIndex);
        return FALSE;
    }

    gboolean isSuccess = TRUE;
    GString* message = g_string_new(NULL);
    g_string_printf(message, "found edge %li (from %s to %s)", (glong)edgeIndex, fromIDStr, toIDStr);

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* weightKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_WEIGHT);
    gdouble weightValue;
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, weightKey) &&
            _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_WEIGHT, &weightValue)) {
        if(weightValue >= 0.0f) {
            g_string_append_printf(message, " %s='%f'", weightKey, weightValue);
        } else {
            /* its an error if they gave a value that is incorrect */
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') must be non-negative",
                    weightKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    } else {
        tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing or NAN",
                weightKey, (glong)edgeIndex, fromIDStr, toIDStr);
        isSuccess = FALSE;
    }

    gboolean isValidEmission = FALSE;

    /* this attribute is required, so it is an error if it doesn't exist */
    const gchar* typeKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_TYPE);
    if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, typeKey)) {
        const gchar* typeStr;
        if(_tgenmarkovmodel_findEdgeAttributeString(mmodel, edgeIndex, EDGE_ATTR_TYPE, &typeStr)) {
            g_string_append_printf(message, " %s='%s'", typeKey, typeStr);

            if(_tgenmarkovmodel_edgeTypeIsEqual(typeStr, EDGE_TYPE_TRANSITION)) {
                if(_tgenmarkovmodel_vertexIDIsEmission(fromIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of source vertex on edge %li (from '%s' to '%s') must not be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                }

                if(_tgenmarkovmodel_vertexIDIsEmission(toIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of destination vertex on edge %li (from '%s' to '%s') must not be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                }
            } else if(_tgenmarkovmodel_edgeTypeIsEqual(typeStr, EDGE_TYPE_EMISSION)) {
                isValidEmission = TRUE;

                if(_tgenmarkovmodel_vertexIDIsEmission(fromIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of source vertex on edge %li (from '%s' to '%s') must not be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                    isValidEmission = FALSE;
                }

                if(!_tgenmarkovmodel_vertexIDIsEmission(toIDStr)) {
                    /* TODO, we could lookup the vertex and check its type attribute */
                    tgen_warning("id of destination vertex on edge %li (from '%s' to '%s') must be an emission type vertex",
                            (glong)edgeIndex, fromIDStr, toIDStr);
                    isSuccess = FALSE;
                    isValidEmission = FALSE;
                }
            } else {
                tgen_warning("required attribute '%s' value '%s' on edge %li (from '%s' to '%s') is invalid, "
                        "need '%s' or '%s'", typeKey, typeStr,
                        (glong)edgeIndex, fromIDStr, toIDStr,
                        _tgenmarkovmodel_edgeTypeToString(EDGE_TYPE_TRANSITION),
                        _tgenmarkovmodel_edgeTypeToString(EDGE_TYPE_EMISSION));
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is NULL",
                    typeKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    } else {
        tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing",
                typeKey, (glong)edgeIndex, fromIDStr, toIDStr);
        isSuccess = FALSE;
    }

    if(isValidEmission) {
        /* this attribute is required, so it is an error if it doesn't exist */
        const gchar* muKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_LOGNORMMU);
        gdouble muValue;
        if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, muKey) &&
                _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_LOGNORMMU, &muValue)) {
            if(muValue >= 0.0f) {
                g_string_append_printf(message, " %s='%f'", muKey, muValue);
            } else {
                /* its an error if they gave a value that is incorrect */
                tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') must be non-negative",
                        muKey, (glong)edgeIndex, fromIDStr, toIDStr);
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing or NAN",
                    muKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }

        /* this attribute is required, so it is an error if it doesn't exist */
        const gchar* sigmaKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_LOGNORMSIGMA);
        gdouble sigmaValue;
        if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, sigmaKey) &&
                _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_LOGNORMSIGMA, &sigmaValue)) {
            if(sigmaValue >= 0.0f) {
                g_string_append_printf(message, " %s='%f'", sigmaKey, sigmaValue);
            } else {
                /* its an error if they gave a value that is incorrect */
                tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') must be non-negative",
                        sigmaKey, (glong)edgeIndex, fromIDStr, toIDStr);
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing or NAN",
                    sigmaKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }

        /* this attribute is required, so it is an error if it doesn't exist */
        const gchar* lambdaKey = _tgenmarkovmodel_edgeAttributeToString(EDGE_ATTR_EXPLAMBDA);
        gdouble lambdaValue;
        if(igraph_cattribute_has_attr(mmodel->graph, IGRAPH_ATTRIBUTE_EDGE, lambdaKey) &&
                _tgenmarkovmodel_findEdgeAttributeDouble(mmodel, edgeIndex, EDGE_ATTR_EXPLAMBDA, &lambdaValue)) {
            if(lambdaValue >= 0.0f) {
                g_string_append_printf(message, " %s='%f'", lambdaKey, lambdaValue);
            } else {
                /* its an error if they gave a value that is incorrect */
                tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') must be non-negative",
                        lambdaKey, (glong)edgeIndex, fromIDStr, toIDStr);
                isSuccess = FALSE;
            }
        } else {
            tgen_warning("required attribute '%s' on edge %li (from '%s' to '%s') is missing or NAN",
                    lambdaKey, (glong)edgeIndex, fromIDStr, toIDStr);
            isSuccess = FALSE;
        }
    }

    tgen_debug("%s", message->str);

    g_string_free(message, TRUE);

    return isSuccess;
}

static gboolean _tgenmarkovmodel_validateEdges(TGenMarkovModel* mmodel) {
    TGEN_ASSERT(mmodel);
    g_assert(mmodel->graph);

    gboolean isSuccess = TRUE;

    /* we will iterate through the edges */
    igraph_eit_t edgeIterator;
    gint result = igraph_eit_create(mmodel->graph, igraph_ess_all(IGRAPH_EDGEORDER_ID), &edgeIterator);

    if(result != IGRAPH_SUCCESS) {
        tgen_warning("igraph_eit_create return non-success code %i", result);
        return FALSE;
    }

    /* count the edges as we iterate */
    while (!IGRAPH_EIT_END(edgeIterator)) {
        igraph_integer_t edgeIndex = IGRAPH_EIT_GET(edgeIterator);

        /* call the hook function for each edge */
        isSuccess = _tgenmarkovmodel_checkEdgeAttributes(mmodel, edgeIndex);
        if(!isSuccess) {
            break;
        }

        IGRAPH_EIT_NEXT(edgeIterator);
    }

    igraph_eit_destroy(&edgeIterator);

    return isSuccess;
}

static igraph_t* _tgenmarkovmodel_loadGraph(const gchar* graphFileName) {
    if(!graphFileName) {
        tgen_warning("We failed to load the markov model graph because the filename was NULL");
        return NULL;
    }

    if(!g_file_test(graphFileName, G_FILE_TEST_EXISTS)) {
        tgen_warning("We failed to load the markov model graph because the "
                "given path '%s' does not exist", graphFileName);
        return NULL;
    }

    if(!g_file_test(graphFileName, G_FILE_TEST_IS_REGULAR)) {
        tgen_warning("We failed to load the markov model graph because the file at the "
                "given path '%s' is not a regular file", graphFileName);
        return NULL;
    }

    tgen_debug("Opening markov model graph file '%s'", graphFileName);

    FILE* graphFile = fopen(graphFileName, "r");
    if (!graphFile) {
        tgen_warning("Unable to open markov model graph file at "
                "path '%s', fopen returned NULL with errno %i: %s",
                graphFileName, errno, strerror(errno));
        return NULL;
    }

    igraph_t* graph = g_new0(igraph_t, 1);

    /* make sure we use the correct attribute handler */
    igraph_i_set_attribute_table(&igraph_cattribute_table);

    gint result = igraph_read_graph_graphml(graph, graphFile, 0);
    fclose(graphFile);

    if (result != IGRAPH_SUCCESS) {
        if(result == IGRAPH_PARSEERROR) {
            tgen_warning("IGraph reported that there was either a problem reading "
                    "the markov model graph file at path '%s', or that the file "
                    "was syntactically incorrect.", graphFileName);
        } else if(result == IGRAPH_UNIMPLEMENTED) {
            tgen_warning("We are unable to read the markov model graph at path '%s'"
                    "because IGraph was not compiled with support for graphml.",
                    graphFileName);
        }

        tgen_warning("Loading the markov model at path '%s' failed.", graphFileName);
        g_free(graph);
        return NULL;
    }

    tgen_info("Successfully read and parsed markov model graph file at path '%s'", graphFileName);
    return graph;
}

static void _tgenmarkovmodel_free(TGenMarkovModel* mmodel) {
    TGEN_ASSERT(mmodel);
    g_assert(mmodel->refcount == 0);

    if(mmodel->graph) {
        igraph_destroy(mmodel->graph);
        g_free(mmodel->graph);
        mmodel->graph = NULL;
    }

    mmodel->magic = 0;
    g_free(mmodel);
}

void tgenmarkovmodel_ref(TGenMarkovModel* mmodel) {
    TGEN_ASSERT(mmodel);
    mmodel->refcount++;
}

void tgenmarkovmodel_unref(TGenMarkovModel* mmodel) {
    TGEN_ASSERT(mmodel);
    if (--(mmodel->refcount) == 0) {
        _tgenmarkovmodel_free(mmodel);
    }
}

TGenMarkovModel* tgenmarkovmodel_new(const gchar* modelPath) {
    TGenMarkovModel* mmodel = g_new0(TGenMarkovModel, 1);
    mmodel->magic = TGEN_MAGIC;
    mmodel->refcount = 1;

    mmodel->graph = _tgenmarkovmodel_loadGraph(modelPath);

    if(!mmodel->graph) {
        tgenmarkovmodel_unref(mmodel);
        tgen_info("Failed to create markov model object");
        return NULL;
    }

    tgen_info("Starting graph validation on markov model at path '%s'", modelPath);

    gboolean verticesPassed = _tgenmarkovmodel_validateVertices(mmodel, &(mmodel->startVertexID));
    if(verticesPassed) {
        tgen_info("Markov model at path '%s' passed vertex validation", modelPath);
    } else {
        tgen_warning("Markov model at path '%s' failed vertex validation", modelPath);
    }

    gboolean edgesPassed = _tgenmarkovmodel_validateEdges(mmodel);
    if(edgesPassed) {
        tgen_info("Markov model at path '%s' passed edge validation", modelPath);
    } else {
        tgen_warning("Markov model at path '%s' failed edge validation", modelPath);
    }

    if(!verticesPassed || !edgesPassed) {
        tgenmarkovmodel_unref(mmodel);
        tgen_info("Failed to create markov model object");
        return NULL;
    }

    tgen_info("Successfully validated markov model graph at "
            "path '%s', found start vertex at index %i", modelPath, (int)mmodel->startVertexID);

    return mmodel;
}
