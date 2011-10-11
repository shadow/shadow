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

#include <glib.h>
#include "shadow.h"

topology_tp topology_create() {
	topology_tp g = g_new0(topology_t, 1);

	g->edges = g_queue_new();
	g->vertices = g_queue_new();
	g->vertices_map = g_hash_table_new(g_int_hash, g_int_equal);

	g->runahead_min = 0;
	g->runahead_max = 0;

	g->is_dirty = 1;

	return g;
}

static void topology_track_minmax(topology_tp g, CumulativeDistribution* cdf) {
	if(g != NULL && cdf != NULL) {
		guint min = (guint) cdf_getMinimumValue(cdf);
		guint max = (guint) cdf_getMaximumValue(cdf);

		if(g->runahead_min == 0 || min < g->runahead_min) {
			g->runahead_min = min;
		}
		if(g->runahead_max == 0 || max > g->runahead_max) {
			g->runahead_max = max;
		}

		/* TODO should not be reliant on worker, instead pass in minimum when creating the topology */
		Worker* worker = worker_getPrivate();
		SimulationTime t = engine_getMinTimeJump(worker->cached_engine);
		if(g->runahead_min < t) {
			g->runahead_min = (guint)t;
		}
	}
}

static gdouble topology_bound_reliability(gdouble reliability) {
	if(reliability < 0.0) {
		reliability = 0.0;
	}
	if(reliability > 1.0) {
		reliability = 1.0;
	}
	return reliability;
}

void topology_add_vertex(topology_tp g, GQuark network_id, CumulativeDistribution* latency_cdf, gdouble reliablity) {
	reliablity = topology_bound_reliability(reliablity);
	if(g != NULL) {
		simnet_vertex_tp v = g_hash_table_lookup(g->vertices_map, &network_id);
		if(v == NULL) {
			v = g_new0(simnet_vertex_t, 1);

			v->id = network_id;
			v->intranet_latency = latency_cdf;
			v->reliablity = reliablity;

			v->edges = g_hash_table_new(g_int_hash, g_int_equal);

			g_queue_push_tail(g->vertices, v);
			g_hash_table_insert(g->vertices_map, &(v->id), v);

			topology_track_minmax(g, latency_cdf);

			g->is_dirty = 1;
		} else {
			critical("topology_add_vertex: id %u already exists\n", network_id);
		}
	}
}

void topology_add_edge(topology_tp g, GQuark id1, CumulativeDistribution* latency_cdf_1to2, gdouble reliablity_1to2, GQuark id2, CumulativeDistribution* latency_cdf_2to1, gdouble reliablity_2to1) {
	reliablity_1to2 = topology_bound_reliability(reliablity_1to2);
	reliablity_2to1 = topology_bound_reliability(reliablity_2to1);

	if(g != NULL) {
		simnet_vertex_tp v1 = g_hash_table_lookup(g->vertices_map, &id1);
		simnet_vertex_tp v2 = g_hash_table_lookup(g->vertices_map, &id2);

		if(v1 != NULL && v2 != NULL) {
			simnet_edge_tp e = g_new0(simnet_edge_t, 1);

			e->vertex1 = v1;
			e->internet_latency_1to2 = latency_cdf_1to2;
			e->reliablity_1to2 = reliablity_1to2;
			e->vertex2 = v2;
			e->internet_latency_2to1 = latency_cdf_2to1;
			e->reliablity_2to1 = reliablity_2to1;

			g_queue_push_tail(g->edges, e);
			g_hash_table_insert(v1->edges, &(e->vertex2->id), e);
			g_hash_table_insert(v2->edges, &(e->vertex1->id), e);

			topology_track_minmax(g, latency_cdf_1to2);
			topology_track_minmax(g, latency_cdf_2to1);

			g->is_dirty = 1;
		} else {
			critical("topology_add_edge: edge endpoint(s) %u and/or $u missing\n", id1, id2);
		}
	}
}

void topology_destroy(topology_tp g) {
	if(g != NULL) {
		if(g->edges != NULL) {
			GList* edge = g_queue_peek_head_link(g->edges);
			while(edge != NULL) {
				simnet_edge_tp e = edge->data;
				g_free(e);
                                edge = edge->next;
			}
			g_queue_free(g->edges);
		}

		if(g->vertices != NULL) {
			GList* vert = g_queue_peek_head_link(g->vertices);
			while(vert != NULL) {
				simnet_vertex_tp v = vert->data;
				g_hash_table_destroy(v->edges);
				g_free(v);
                                vert = vert->next;
			}
			g_queue_free(g->vertices);
		}

		g_hash_table_destroy(g->vertices_map);

		g_free(g);
	}
}

gdouble topology_end2end_latency(topology_tp g, GQuark src_network_id, GQuark dst_network_id) {
	gdouble milliseconds_latency = -1;

	if(g != NULL) {
		/* find latency for a node in src_network to a node in dst_network */
		simnet_vertex_tp vertex = g_hash_table_lookup(g->vertices_map, &src_network_id);
		if(vertex != NULL) {
			if(src_network_id == dst_network_id) {
				/* intranet */
				milliseconds_latency = cdf_getRandomValue(vertex->intranet_latency);
			} else {
				/* internet */
				simnet_edge_tp edge = g_hash_table_lookup(vertex->edges, &dst_network_id);
				if(edge != NULL) {
					if(vertex->id == edge->vertex1->id) {
						milliseconds_latency = cdf_getRandomValue(edge->internet_latency_1to2);
					} else if(vertex->id == edge->vertex2->id) {
						milliseconds_latency = cdf_getRandomValue(edge->internet_latency_2to1);
					} else {
						critical("topology_end2end_latency: no connection between networks %u and $u\n", src_network_id, dst_network_id);
					}
				}
			}
		}
	}

	if(milliseconds_latency < g->runahead_min)
		milliseconds_latency = (gdouble) g->runahead_min;

	return milliseconds_latency;
}

gdouble topology_end2end_reliablity(topology_tp g, GQuark src_network_id, GQuark dst_network_id) {
	gdouble reliability = -1;

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
						critical("topology_end2end_reliablity: no connection between networks %u and $u\n", src_network_id, dst_network_id);
					}
				}
			}
		}
	}

	return reliability;
}
