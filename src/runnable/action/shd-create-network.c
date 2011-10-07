/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
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

#include "shadow.h"

RunnableVTable createnetwork_vtable = {
	(RunnableRunFunc) createnetwork_run,
	(RunnableFreeFunc) createnetwork_free,
	MAGIC_VALUE
};

CreateNetworkAction* createnetwork_new(GString* name, GString* latencyCDFName,
		gdouble reliability)
{
	g_assert(name && latencyCDFName);
	CreateNetworkAction* action = g_new0(CreateNetworkAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnetwork_vtable);

	action->name = g_string_new(name->str);
	action->latencyCDFName = g_string_new(latencyCDFName->str);
	action->reliability = reliability;

	return action;
}

void createnetwork_run(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

//				/* make sure we have the dsim variable data */
//				if(dsimop->arguments[0].v.var_val &&
//						dsimop->arguments[0].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
//						dsimop->arguments[0].v.var_val->data) {
//					guint netid = sim_master_dsimop_helper(dsimop, master->network_tracking, dsim_vartracker_type_nettrack);
//					guint cdf_id = ((sim_master_tracker_tp)dsimop->arguments[0].v.var_val->data)->id;
//					gdouble reliability = dsimop->arguments[1].v.gdouble_val;
//
//					/* get the cdf used for latency */
//					sim_master_tracker_tp cdf_tracker = g_hash_table_lookup(master->cdf_tracking, &cdf_id);
//					if(cdf_tracker != NULL && cdf_tracker->value != NULL) {
//						/* add it to our topology */
//						cdf_tp cdf = cdf_tracker->value;
//						simnet_graph_add_vertex(master->network_topology, netid, cdf, reliability);
//					}
//				}
//
//				/* normally this would happen at the event exe time */
//
//				/* build up our knowledge of the network */
//				cdf_tp cdf = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_gintra_latency);
//				simnet_graph_add_vertex(wo->network_topology, op->id, cdf, op->reliability);
//
//				/* vci needs ids to look up graph properties */
//				vci_network_create(wo->vci_mgr, op->id);
}

void createnetwork_free(CreateNetworkAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->name, TRUE);
	g_string_free(action->latencyCDFName, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
