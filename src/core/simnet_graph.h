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

#ifndef SIMNET_GRAPH_H_
#define SIMNET_GRAPH_H_

#include <glib.h>
#include <glib-2.0/glib.h>
#include "shd-cdf.h"

#define RUNAHEAD_FLOOR_MS 10

typedef struct simnet_vertex_s {
	guint id;

	/* connections to other nodes */
	GHashTable *edges;

	/* gintranet properties */
	cdf_tp gintranet_latency;
	gdouble reliablity;
} simnet_vertex_t, *simnet_vertex_tp;

typedef struct simnet_edge_s {
	/* both ends of this connection */
	simnet_vertex_tp vertex1;
	simnet_vertex_tp vertex2;

	/* ginternet properties between the vertex1 and vertex2 */
	cdf_tp ginternet_latency_1to2;
	gdouble reliablity_1to2;
	cdf_tp ginternet_latency_2to1;
	gdouble reliablity_2to1;
} simnet_edge_t, *simnet_edge_tp;

typedef struct simnet_graph_s {
	gint is_dirty;

	/* holds all networks, of type sim_net_vertex_tp */
	GQueue *vertices;
	GQueue *edges;
	GHashTable *vertices_map;

	/* the min/max latency between networks - used for runahead */
	guint runahead_min;
	guint runahead_max;
} simnet_graph_t, *simnet_graph_tp;

simnet_graph_tp simnet_graph_create();
void simnet_graph_destroy(simnet_graph_tp g);
void simnet_graph_add_vertex(simnet_graph_tp g, guint network_id, cdf_tp latency_cdf, gdouble reliablity);
void simnet_graph_add_edge(simnet_graph_tp g, guint id1, cdf_tp latency_cdf_1to2, gdouble reliablity_1to2, guint id2, cdf_tp latency_cdf_2to1, gdouble reliablity_2to1);
gdouble simnet_graph_end2end_latency(simnet_graph_tp g, guint src_network_id, guint dst_network_id);
gdouble simnet_graph_end2end_reliablity(simnet_graph_tp g, guint src_network_id, guint dst_network_id);

#endif /* SIMNET_GRAPH_H_ */
