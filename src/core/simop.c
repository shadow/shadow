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

#include <sys/time.h>
#include <stdlib.h>
#include "simop.h"
#include "evtracker.h"
#include "sim.h"
#include "dsim_utils.h"
#include "vci.h"

simop_list_tp simop_list_create (void) {
	return evtracker_create(10, 1);
}


void simop_list_destroy( simop_list_tp list ) {
	simop_tp sop;
	while((sop=evtracker_get_nextevent(list, NULL, 1))){
		simop_destroy(sop);
	}
	evtracker_destroy(list);
}

void simop_list_add( simop_list_tp list, simop_tp op, ptime_t time) {
	evtracker_insert_event(list, time, op);
}

int simop_list_size( simop_list_tp list ) {
	return evtracker_get_numevents(list);
}

simop_tp simop_get_next( simop_list_tp list, ptime_t * time ) {
	return evtracker_get_nextevent(list, time, 1);
}

simop_tp simop_look_next( simop_list_tp list, ptime_t * time) {
	return evtracker_get_nextevent(list, time, 0);
}

void simop_destroy(simop_tp simop) {
	if(simop != NULL) {
		free(simop->operation);
		free(simop);
	}
}

nbdf_tp simop_nbdf_encode(operation_tp dsimop, unsigned int tracking_id) {
	nbdf_tp nb = NULL;
	nbdf_tp nb_outer = NULL;

	if(!dsimop)
		return NULL;

	switch(dsimop->type){
		case OP_LOAD_PLUGIN:
		case OP_LOAD_CDF: {
			char* filepath = dsimop->arguments[0].v.string_val;

			nb = nbdf_construct("is",
					tracking_id,
					filepath);

			break;
		}
		case OP_GENERATE_CDF: {
			unsigned int cdf_base_center = (unsigned int)(dsimop->arguments[0].v.double_val);
			unsigned int cdf_base_width = (unsigned int)(dsimop->arguments[1].v.double_val);
			unsigned int cdf_tail_width = (unsigned int)(dsimop->arguments[2].v.double_val);

			nb = nbdf_construct("iiii",
					tracking_id,
					cdf_base_center,
					cdf_base_width,
					cdf_tail_width);

			break;
		}
		case OP_CREATE_NETWORK: {
			unsigned int cdf_id = ((sim_master_tracker_tp)dsimop->arguments[0].v.var_val->data)->id;
			double reliability = dsimop->arguments[1].v.double_val;

			nb = nbdf_construct("iid",
					tracking_id,
					cdf_id,
					reliability);

			break;
		}
		case OP_CONNECT_NETWORKS: {
			unsigned int net1_id = ((sim_master_tracker_tp)dsimop->arguments[0].v.var_val->data)->id;
			unsigned int cdf_id_latency_net1_to_net2 = ((sim_master_tracker_tp)dsimop->arguments[1].v.var_val->data)->id;
			double reliability_net1_to_net2 = dsimop->arguments[2].v.double_val;
			unsigned int net2_id = ((sim_master_tracker_tp)dsimop->arguments[3].v.var_val->data)->id;
			unsigned int cdf_id_latency_net2_to_net1 = ((sim_master_tracker_tp)dsimop->arguments[4].v.var_val->data)->id;
			double reliability_net2_to_net1 = dsimop->arguments[5].v.double_val;

			nb = nbdf_construct("iidiid",
					net1_id,
					cdf_id_latency_net1_to_net2,
					reliability_net1_to_net2,
					net2_id,
					cdf_id_latency_net2_to_net1,
					reliability_net2_to_net1);

			break;
		}
		case OP_CREATE_HOSTNAME: {
			char* base_hostname = dsimop->arguments[0].v.string_val;

			nb = nbdf_construct("is",
					tracking_id,
					base_hostname);

			break;
		}
		case OP_CREATE_NODES: {
			unsigned int plugin_id = ((sim_master_tracker_tp)dsimop->arguments[1].v.var_val->data)->id;
			unsigned int network_id = ((sim_master_tracker_tp)dsimop->arguments[2].v.var_val->data)->id;
			unsigned int base_hostname_id = ((sim_master_tracker_tp)dsimop->arguments[3].v.var_val->data)->id;

			unsigned int cdf_id_bandwidth_up = 0;
			if(dsimop->arguments[4].v.var_val->data_type == dsim_vartracker_type_cdftrack) {
				cdf_id_bandwidth_up = ((sim_master_tracker_tp)dsimop->arguments[4].v.var_val->data)->id;
			}

			unsigned int cdf_id_bandwidth_down = 0;
			if(dsimop->arguments[5].v.var_val->data_type == dsim_vartracker_type_cdftrack) {
				cdf_id_bandwidth_down = ((sim_master_tracker_tp)dsimop->arguments[5].v.var_val->data)->id;
			}

			unsigned int cdf_id_cpu_speed = ((sim_master_tracker_tp)dsimop->arguments[6].v.var_val->data)->id;

			char* plugin_args = dsimop->arguments[7].v.string_val;

			nb = nbdf_construct("iiiiiiis",
					plugin_id,
					network_id,
					base_hostname_id,
					tracking_id, /* NOTE: used here to make unique hostnames */
					cdf_id_bandwidth_up,
					cdf_id_bandwidth_down,
					cdf_id_cpu_speed,
					plugin_args);

			break;
		}
		case OP_END: {
			nb = nbdf_construct("t",
					dsimop->target_time);

			break;
		}
		default: {
			/* default has no inner frame */
			break;
		}
	}

	nb_outer = nbdf_construct("itn", dsimop->type, dsimop->target_time, nb);
	nbdf_free(nb);

	return nb_outer;
}

simop_tp simop_nbdf_decode(nbdf_tp nb) {
	nbdf_tp inner;
	simop_tp simop = malloc(sizeof(simop_t));

	nbdf_read(nb, "itn", &simop->type, &simop->target_time, &inner);

	switch(simop->type){
		case OP_LOAD_PLUGIN: {
			simop_load_plugin_tp op = malloc(sizeof(simop_load_plugin_t));

			nbdf_read(inner, "is",
					&op->id,
					sizeof(op->filepath),
					op->filepath);

			simop->operation = op;

			break;
		}
		case OP_LOAD_CDF: {
			simop_load_cdf_tp op = malloc(sizeof(simop_load_cdf_t));

			nbdf_read(inner, "is",
					&op->id,
					sizeof(op->filepath),
					op->filepath);

			simop->operation = op;

			break;
		}
		case OP_GENERATE_CDF: {
			simop_generate_cdf_tp op = malloc(sizeof(simop_generate_cdf_t));

			nbdf_read(inner, "iiii",
					&op->id,
					&op->base_delay,
					&op->base_width,
					&op->tail_width);

			simop->operation = op;

			break;
		}
		case OP_CREATE_NETWORK: {
			simop_create_network_tp op = malloc(sizeof(simop_create_network_t));

			nbdf_read(inner, "iid",
					&op->id,
					&op->cdf_id_intra_latency,
					&op->reliability);

			simop->operation = op;

			break;
		}
		case OP_CONNECT_NETWORKS: {
			simop_connect_networks_tp op = malloc(sizeof(simop_connect_networks_t));

			nbdf_read(inner, "iidiid",
					&op->network1_id,
					&op->cdf_id_latency_1to2,
					&op->reliability_1to2,
					&op->network2_id,
					&op->cdf_id_latency_2to1,
					&op->reliability_2to1);

			simop->operation = op;

			break;
		}
		case OP_CREATE_HOSTNAME: {
			simop_create_hostname_tp op = malloc(sizeof(simop_create_hostname_t));

			nbdf_read(inner, "is",
					&op->id,
					sizeof(op->base_hostname),
					op->base_hostname);

			simop->operation = op;

			break;
		}
		case OP_CREATE_NODES: {
			simop_create_nodes_tp op = malloc(sizeof(simop_create_nodes_t));

			nbdf_read(inner, "iiiiiiis",
					&op->plugin_id,
					&op->network_id,
					&op->hostname_id,
					&op->hostname_unique_counter,
					&op->cdf_id_bandwidth_up,
					&op->cdf_id_bandwidth_down,
					&op->cdf_id_cpu_speed,
					sizeof(op->cl_args),
					op->cl_args);

			simop->operation = op;

			break;
		}
		case OP_END: {
			simop_end_tp op = malloc(sizeof(simop_end_t));

			nbdf_read(inner, "t", &op->end_time);

			simop->operation = op;

			break;
		}
		default: {
			/* default has no inner frame */
			break;
		}
	}

	nbdf_free(inner);

	return simop;
}
