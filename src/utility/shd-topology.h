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

#ifndef SHD_TOPOLOGY_H_
#define SHD_TOPOLOGY_H_

#include "shadow.h"

typedef struct simnet_vertex_s {
	GQuark id;

	/* connections to other nodes */
	GHashTable *edges;

	/* gintranet properties */
	CumulativeDistribution* intranet_latency;
	gdouble reliablity;
} simnet_vertex_t, *simnet_vertex_tp;

typedef struct simnet_edge_s {
	/* both ends of this connection */
	simnet_vertex_tp vertex1;
	simnet_vertex_tp vertex2;

	/* ginternet properties between the vertex1 and vertex2 */
	CumulativeDistribution* internet_latency_1to2;
	gdouble reliablity_1to2;
	CumulativeDistribution* internet_latency_2to1;
	gdouble reliablity_2to1;
} simnet_edge_t, *simnet_edge_tp;

typedef struct topology_s {
	gint is_dirty;

	/* holds all networks, of type sim_net_vertex_tp */
	GQueue *vertices;
	GQueue *edges;
	GHashTable *vertices_map;

	/* the min/max latency between networks - used for runahead */
	guint runahead_min;
	guint runahead_max;
} topology_t, *topology_tp;

topology_tp topology_create();
void topology_destroy(topology_tp g);
void topology_add_vertex(topology_tp g, GQuark network_id, CumulativeDistribution* latency_cdf, gdouble reliablity);
void topology_add_edge(topology_tp g, GQuark id1, CumulativeDistribution* latency_cdf_1to2, gdouble reliablity_1to2, GQuark id2, CumulativeDistribution* latency_cdf_2to1, gdouble reliablity_2to1);
gdouble topology_end2end_latency(topology_tp g, GQuark src_network_id, GQuark dst_network_id);
gdouble topology_end2end_reliablity(topology_tp g, GQuark src_network_id, GQuark dst_network_id);

#endif /* SHD_TOPOLOGY_H_ */
