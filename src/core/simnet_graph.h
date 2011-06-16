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

#include "list.h"
#include "hashtable.h"
#include "shd-cdf.h"

#define RUNAHEAD_FLOOR_MS 10

typedef struct simnet_vertex_s {
	unsigned int id;

	/* connections to other nodes */
	hashtable_tp edges;

	/* intranet properties */
	cdf_tp intranet_latency;
	double reliablity;
} simnet_vertex_t, *simnet_vertex_tp;

typedef struct simnet_edge_s {
	/* both ends of this connection */
	simnet_vertex_tp vertex1;
	simnet_vertex_tp vertex2;

	/* internet properties between the vertex1 and vertex2 */
	cdf_tp internet_latency_1to2;
	double reliablity_1to2;
	cdf_tp internet_latency_2to1;
	double reliablity_2to1;
} simnet_edge_t, *simnet_edge_tp;

typedef struct simnet_graph_s {
	int is_dirty;

	/* holds all networks, of type sim_net_vertex_tp */
	list_tp vertices;
	list_tp edges;
	hashtable_tp vertices_map;

	/* the min/max latency between networks - used for runahead */
	unsigned int runahead_min;
	unsigned int runahead_max;
} simnet_graph_t, *simnet_graph_tp;

simnet_graph_tp simnet_graph_create();
void simnet_graph_destroy(simnet_graph_tp g);
void simnet_graph_add_vertex(simnet_graph_tp g, unsigned int network_id, cdf_tp latency_cdf, double reliablity);
void simnet_graph_add_edge(simnet_graph_tp g, unsigned int id1, cdf_tp latency_cdf_1to2, double reliablity_1to2, unsigned int id2, cdf_tp latency_cdf_2to1, double reliablity_2to1);
double simnet_graph_end2end_latency(simnet_graph_tp g, unsigned int src_network_id, unsigned int dst_network_id);
double simnet_graph_end2end_reliablity(simnet_graph_tp g, unsigned int src_network_id, unsigned int dst_network_id);

#endif /* SIMNET_GRAPH_H_ */
