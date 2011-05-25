/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2006-2009 Tyson Malchow <tyson.malchow@gmail.com>
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>

#include "simnet_graph.h"
#include "sysconfig.h"
#include "log.h"
#include "shd-cdf.h"
#include "utility.h"

simnet_graph_tp simnet_graph_create() {
	simnet_graph_tp g = malloc(sizeof(simnet_graph_t));

	g->edges = list_create();
	g->vertices = list_create();
	g->vertices_map = g_hash_table_new(g_int_hash, g_int_equal);

	g->runahead_min = 0;
	g->runahead_max = 0;

	g->is_dirty = 1;

	return g;
}

static void simnet_graph_track_minmax(simnet_graph_tp g, cdf_tp cdf) {
	if(g != NULL && cdf != NULL) {
		unsigned int min = (unsigned int) cdf_min_value(cdf);
		unsigned int max = (unsigned int) cdf_max_value(cdf);

		if(g->runahead_min == 0 || min < g->runahead_min) {
			g->runahead_min = min;
		}
		if(g->runahead_max == 0 || max > g->runahead_max) {
			g->runahead_max = max;
		}
	}
}

static double simnet_graph_bound_reliability(double reliability) {
	if(reliability < 0.0) {
		reliability = 0.0;
	}
	if(reliability > 1.0) {
		reliability = 1.0;
	}
	return reliability;
}

void simnet_graph_add_vertex(simnet_graph_tp g, unsigned int network_id, cdf_tp latency_cdf, double reliablity) {
	reliablity = simnet_graph_bound_reliability(reliablity);
	if(g != NULL) {
		simnet_vertex_tp v = g_hash_table_lookup(g->vertices_map, &network_id);
		if(v == NULL) {
			v = malloc(sizeof(simnet_vertex_t));

			v->id = network_id;
			v->intranet_latency = latency_cdf;
			v->reliablity = reliablity;

			v->edges = g_hash_table_new(g_int_hash, g_int_equal);

			list_push_back(g->vertices, v);
			g_hash_table_insert(g->vertices_map, int_key(network_id), v);

			simnet_graph_track_minmax(g, latency_cdf);

			g->is_dirty = 1;
		} else {
			dlogf(LOG_WARN, "simnet_graph_add_vertex: id %u already exists\n", network_id);
		}
	}
}

void simnet_graph_add_edge(simnet_graph_tp g, unsigned int id1, cdf_tp latency_cdf_1to2, double reliablity_1to2, unsigned int id2, cdf_tp latency_cdf_2to1, double reliablity_2to1) {
	reliablity_1to2 = simnet_graph_bound_reliability(reliablity_1to2);
	reliablity_2to1 = simnet_graph_bound_reliability(reliablity_2to1);

	if(g != NULL) {
		simnet_vertex_tp v1 = g_hash_table_lookup(g->vertices_map, &id1);
		simnet_vertex_tp v2 = g_hash_table_lookup(g->vertices_map, &id2);

		if(v1 != NULL && v2 != NULL) {
			simnet_edge_tp e = malloc(sizeof(simnet_edge_t));

			e->vertex1 = v1;
			e->internet_latency_1to2 = latency_cdf_1to2;
			e->reliablity_1to2 = reliablity_1to2;
			e->vertex2 = v2;
			e->internet_latency_2to1 = latency_cdf_2to1;
			e->reliablity_2to1 = reliablity_2to1;

			list_push_back(g->edges, e);
			g_hash_table_insert(v1->edges, int_key(id2), e);
			g_hash_table_insert(v2->edges, int_key(id1), e);

			simnet_graph_track_minmax(g, latency_cdf_1to2);
			simnet_graph_track_minmax(g, latency_cdf_2to1);

			g->is_dirty = 1;
		} else {
			dlogf(LOG_WARN, "simnet_graph_add_edge: edge endpoint(s) %u and/or $u missing\n", id1, id2);
		}
	}
}

void simnet_graph_destroy(simnet_graph_tp g) {
	if(g != NULL) {
		if(g->edges != NULL) {
			list_iter_tp edge_iter = list_iterator_create(g->edges);
			while(list_iterator_hasnext(edge_iter)) {
				simnet_edge_tp e = list_iterator_getnext(edge_iter);
				free(e);
			}
			list_iterator_destroy(edge_iter);
			list_destroy(g->edges);
		}

		if(g->vertices != NULL) {
			list_iter_tp vert_iter = list_iterator_create(g->vertices);
			while(list_iterator_hasnext(vert_iter)) {
				simnet_vertex_tp v = list_iterator_getnext(vert_iter);
				g_hash_table_destroy(v->edges);
				free(v);
			}
			list_iterator_destroy(vert_iter);
			list_destroy(g->vertices);
		}

		g_hash_table_destroy(g->vertices_map);

		free(g);
	}
}

double simnet_graph_end2end_latency(simnet_graph_tp g, unsigned int src_network_id, unsigned int dst_network_id) {
	double milliseconds_latency = -1;

	if(g != NULL) {
		/* find latency for a node in src_network to a node in dst_network */
		simnet_vertex_tp vertex = g_hash_table_lookup(g->vertices_map, &src_network_id);
		if(vertex != NULL) {
			if(src_network_id == dst_network_id) {
				/* intranet */
				milliseconds_latency = cdf_random_value(vertex->intranet_latency);
			} else {
				/* internet */
				simnet_edge_tp edge = g_hash_table_lookup(vertex->edges, &dst_network_id);
				if(edge != NULL) {
					if(vertex->id == edge->vertex1->id) {
						milliseconds_latency = cdf_random_value(edge->internet_latency_1to2);
					} else if(vertex->id == edge->vertex2->id) {
						milliseconds_latency = cdf_random_value(edge->internet_latency_2to1);
					} else {
						dlogf(LOG_WARN, "simnet_graph_end2end_latency: no connection between networks %u and $u\n", src_network_id, dst_network_id);
					}
				}
			}
		}
	}

	return milliseconds_latency;
}

double simnet_graph_end2end_reliablity(simnet_graph_tp g, unsigned int src_network_id, unsigned int dst_network_id) {
	double reliability = -1;

	if(g != NULL) {
		/* find latency for a node in src_network to a node in dst_network */
		simnet_vertex_tp vertex = g_hash_table_lookup(g->vertices_map, &src_network_id);
		if(vertex != NULL) {
			if(src_network_id == dst_network_id) {
				/* intranet */
				reliability = vertex->reliablity;
			} else {
				/* internet */
				simnet_edge_tp edge = g_hash_table_lookup(vertex->edges, &dst_network_id);
				if(edge != NULL) {
					if(vertex->id == edge->vertex1->id) {
						reliability = edge->reliablity_1to2;
					} else if(vertex->id == edge->vertex2->id) {
						reliability = edge->reliablity_2to1;
					} else {
						dlogf(LOG_WARN, "simnet_graph_end2end_reliablity: no connection between networks %u and $u\n", src_network_id, dst_network_id);
					}
				}
			}
		}
	}

	return reliability;
}
