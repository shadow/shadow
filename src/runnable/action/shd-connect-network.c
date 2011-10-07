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

RunnableVTable connectnetwork_vtable = { (RunnableRunFunc) connectnetwork_run,
		(RunnableFreeFunc) connectnetwork_free, MAGIC_VALUE };

ConnectNetworkAction* connectnetwork_new(GString* networkaName,
		GString* networkbName, GString* latencyabCDFName,
		gdouble reliabilityab, GString* latencybaCDFName,
		gdouble reliabilityba)
{
	g_assert(networkaName && networkbName && latencyabCDFName && latencybaCDFName);
	ConnectNetworkAction* action = g_new0(ConnectNetworkAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &connectnetwork_vtable);

	action->networkaName = g_string_new(networkaName->str);
	action->networkbName = g_string_new(networkbName->str);
	action->latencyabCDFName = g_string_new(latencyabCDFName->str);
	action->latencybaCDFName = g_string_new(latencybaCDFName->str);
	action->reliabilityab = reliabilityab;
	action->reliabilityba = reliabilityba;

	return action;
}

void connectnetwork_run(ConnectNetworkAction* action) {
	MAGIC_ASSERT(action);

//				/* make sure we have the dsim variable data */
//				if(dsimop->arguments[0].v.var_val &&
//						dsimop->arguments[0].v.var_val->data_type == dsim_vartracker_type_nettrack &&
//						dsimop->arguments[0].v.var_val->data &&
//						dsimop->arguments[1].v.var_val &&
//						dsimop->arguments[1].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
//						dsimop->arguments[1].v.var_val->data &&
//						dsimop->arguments[3].v.var_val &&
//						dsimop->arguments[3].v.var_val->data_type == dsim_vartracker_type_nettrack &&
//						dsimop->arguments[3].v.var_val->data &&
//						dsimop->arguments[4].v.var_val &&
//						dsimop->arguments[4].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
//						dsimop->arguments[4].v.var_val->data) {
//
//					guint net1_id = ((sim_master_tracker_tp)dsimop->arguments[0].v.var_val->data)->id;
//					guint cdf_id_latency_net1_to_net2 = ((sim_master_tracker_tp)dsimop->arguments[1].v.var_val->data)->id;
//					gdouble reliability_net1_to_net2 = dsimop->arguments[2].v.gdouble_val;
//					guint net2_id = ((sim_master_tracker_tp)dsimop->arguments[3].v.var_val->data)->id;
//					guint cdf_id_latency_net2_to_net1 = ((sim_master_tracker_tp)dsimop->arguments[4].v.var_val->data)->id;
//					gdouble reliability_net2_to_net1 = dsimop->arguments[5].v.gdouble_val;
//
//
//					/* encode the simop to NBDF */
//					nb_op = simop_nbdf_encode(dsimop, 0);
//
//					/* notify all the workers about the new connection */
//					dvn_packet_route(DVNPACKET_GLOBAL_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_OP, nb_op);
//
//					nbdf_free(nb_op);
//
//					/* get the cdfs used for latency */
//					sim_master_tracker_tp cdf_tracker_1to2 = g_hash_table_lookup(master->cdf_tracking, &cdf_id_latency_net1_to_net2);
//					sim_master_tracker_tp cdf_tracker_2to1 = g_hash_table_lookup(master->cdf_tracking, &cdf_id_latency_net2_to_net1);
//					if(cdf_tracker_1to2 != NULL && cdf_tracker_1to2->value != NULL
//							&& cdf_tracker_2to1 != NULL && cdf_tracker_2to1->value != NULL) {
//						/* add it to our topology */
//						cdf_tp cdf_1to2 = cdf_tracker_1to2->value;
//						cdf_tp cdf_2to1 = cdf_tracker_2to1->value;
//						simnet_graph_add_edge(master->network_topology, net1_id, cdf_1to2, reliability_net1_to_net2, net2_id, cdf_2to1, reliability_net2_to_net1);
//					}
//				}
//
//				/* normally this would happen at the event exe time */
//
//				/* build up our knowledge of the network */
//				cdf_tp cdf_latency_1to2 = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_latency_1to2);
//				cdf_tp cdf_latency_2to1 = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_latency_2to1);
//
//				simnet_graph_add_edge(wo->network_topology, op->network1_id, cdf_latency_1to2, op->reliability_1to2,
//						op->network2_id, cdf_latency_2to1, op->reliability_2to1);
}

void connectnetwork_free(ConnectNetworkAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->networkaName, TRUE);
	g_string_free(action->networkbName, TRUE);
	g_string_free(action->latencyabCDFName, TRUE);
	g_string_free(action->latencybaCDFName, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
