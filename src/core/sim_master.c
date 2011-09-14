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
#include <string.h>

#include "global.h"
#include "sysconfig.h"
#include "sim.h"
#include "routing.h"
#include "nbdf.h"
#include "netconst.h"
#include "rand.h"
#include "simnet_graph.h"

sim_master_tp sim_master_create (gchar * dsim, guint num_slaves) {
	sim_master_tp smaster;
	nbdf_tp start_nb;
	operation_tp op = NULL;

	smaster = malloc(sizeof(*smaster));
	if(!smaster)
		printfault(EXIT_NOMEM, "sim_master_create: Out of memory");

	smaster->num_slaves = num_slaves;
	smaster->num_slaves_complete = 0;
	smaster->network_topology = simnet_graph_create();
	smaster->end_time_found = 0;

	smaster->dsim = dsim_create(dsim);
	if(!smaster->dsim) {
		free(smaster);
		dlogf(LOG_ERR, "SMaster: Invalid DSIM file submitted.\n");
		return NULL;
	}

	smaster->module_tracking = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);
	smaster->cdf_tracking = g_hash_table_new(g_int_hash, g_int_equal);
	smaster->network_tracking = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);
	smaster->base_hostname_tracking = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);

	clock_gettime(CLOCK_MONOTONIC, &smaster->simulation_start);

	debugf( "SMaster: DSIM validated and loaded OK. Simulation master logic instantiated.\n");

	/* spool all DSIM commands to slaves */
	while((op=dsim_get_nextevent(smaster->dsim, NULL, 1)))
		sim_master_opexec(smaster, op);

	if(!smaster->end_time_found) {
		/* EEEE! hay una problema! */
		dlogf(LOG_ERR, "SMaster: DSIM file submitted has no end time. That would take awhile. Aborting.\n");

		sim_master_destroy(smaster);
		return NULL;
	}


	/* use lower and upper bounds of network latencies for runahead
	 * this will cause all worker processes to flip alive and start working! */
	start_nb = nbdf_construct("ii", smaster->network_topology->runahead_max, smaster->network_topology->runahead_min);
	dvn_packet_route(DVNPACKET_GLOBAL_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_START, start_nb);
	nbdf_free(start_nb);

	/* if there are no other slaves, then we've got to send a state frame to the worker(s) so
	 * they don't wait on us.
	 */
	if(num_slaves == 1) {
		nbdf_tp state_frame = nbdf_construct("itttt", 0, PTIME_MAX, PTIME_INVALID, PTIME_MAX, PTIME_MAX);
		dvn_packet_route(DVNPACKET_WORKER_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_STATE, state_frame);
		nbdf_free(state_frame);
	}

	debugf( "SMaster: Sent start frame to all slaves.\n");

	return smaster;
}

static void sim_master_destroy_cdftracker_cb(gpointer key, gpointer value, gpointer param) {
	sim_master_tracker_tp cdf_tracker = value;
	if(cdf_tracker != NULL) {
		cdf_destroy((cdf_tp)cdf_tracker->value);
		free(cdf_tracker);
	}
}

void sim_master_destroy(sim_master_tp sim) {
	if(sim->dsim)
		dsim_destroy(sim->dsim);

	simnet_graph_destroy(sim->network_topology);

	g_hash_table_remove_all(sim->network_tracking);
	g_hash_table_destroy(sim->network_tracking);

	g_hash_table_remove_all(sim->module_tracking);
	g_hash_table_destroy(sim->module_tracking);

	g_hash_table_foreach(sim->cdf_tracking, sim_master_destroy_cdftracker_cb, NULL);
	g_hash_table_destroy(sim->cdf_tracking);

	g_hash_table_remove_all(sim->base_hostname_tracking);
	g_hash_table_destroy(sim->base_hostname_tracking);

	free(sim);

	debugf( "SMaster: Destroyed.\n");
}


void sim_master_deposit(sim_master_tp smaster, gint frametype, nbdf_tp nb) {

	switch(frametype) {
		case SIM_FRAME_DONE_SLAVE:{
			smaster->num_slaves_complete++;
			break;
		}
	}
}

gint sim_master_isdone(sim_master_tp smaster) {
	return smaster->num_slaves == smaster->num_slaves_complete ? 1 : 0;
}

static guint sim_master_dsimop_helper(operation_tp dsimop, GHashTable *tracker_ht, enum dsim_vartype vartype) {
	sim_master_tracker_tp tracker;
	nbdf_tp nb_op;

	if(dsimop->retval) {
		/* need a unique id for tracking, but 0 is reserved */
		guint tracking_id = 0;
		while(tracking_id == 0 || g_hash_table_lookup(tracker_ht, &tracking_id) != NULL) {
			tracking_id = dvn_rand_fast(RAND_MAX);
		}

		/* create the tracker for dsim */
		tracker = malloc(sizeof(sim_master_tracker_t));
		if(!tracker)
			printfault(EXIT_NOMEM, "sim_master_dsimop_helper: Out of memory");
		tracker->id = tracking_id;
		tracker->counter = 0;
		tracker->value = NULL;
		g_hash_table_insert(tracker_ht, &(tracker->id), tracker);

		/* save it to the variable so DSIM has access to it */
		dsimop->retval->data = tracker;
		dsimop->retval->data_type = vartype;

		/* encode the operation to NBDF */
		nb_op = simop_nbdf_encode(dsimop, tracking_id);

		/* notify all the workers about the module load */
		dvn_packet_route(DVNPACKET_GLOBAL_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_OP, nb_op);

		nbdf_free(nb_op);

		return tracking_id;
	}
	return 0;
}

static void sim_master_dsimop_load_plugin(sim_master_tp master, operation_tp dsimop) {
	debugf("SMaster: Parsing DSIM Operation: load_plugin(): '%s'\n", dsimop->arguments[0].v.string_val);
	sim_master_dsimop_helper(dsimop, master->module_tracking, dsim_vartracker_type_modtrack);
}

static void sim_master_dsimop_load_cdf(sim_master_tp master, operation_tp dsimop) {
	gchar* filepath = dsimop->arguments[0].v.string_val;
	debugf("SMaster: Parsing DSIM Operation: load_cdf(): '%s'\n", filepath);

	guint id = sim_master_dsimop_helper(dsimop, master->cdf_tracking, dsim_vartracker_type_cdftrack);

	/* master has to keep track of all cdfs used for latency in order to compute runahead */
	sim_master_tracker_tp cdf_tracker = g_hash_table_lookup(master->cdf_tracking, &id);
	if(cdf_tracker != NULL) {
		cdf_tp cdf = cdf_create(filepath);
		if(cdf != NULL) {
			cdf_tracker->value = cdf;
		}
	}
}

static void sim_master_dsimop_generate_cdf(sim_master_tp master, operation_tp dsimop) {
	debugf("SMaster: Parsing DSIM Operation: generate_cdf()\n");

	guint cdf_base_center = (guint)(dsimop->arguments[0].v.gdouble_val);
	guint cdf_base_width = (guint)(dsimop->arguments[1].v.gdouble_val);
	guint cdf_tail_width = (guint)(dsimop->arguments[2].v.gdouble_val);

	guint id = sim_master_dsimop_helper(dsimop, master->cdf_tracking, dsim_vartracker_type_cdftrack);

	/* master has to keep track of all cdfs used for latency in order to compute runahead */
	sim_master_tracker_tp cdf_tracker = g_hash_table_lookup(master->cdf_tracking, &id);
	if(cdf_tracker != NULL) {
		cdf_tp cdf = cdf_generate(cdf_base_center, cdf_base_width, cdf_tail_width);
		if(cdf != NULL) {
			cdf_tracker->value = cdf;
		}
	}
}

static void sim_master_dsimop_create_network(sim_master_tp master, operation_tp dsimop) {
	debugf("SMaster: Parsing DSIM Operation: create_network()\n");

	/* make sure we have the dsim variable data */
	if(dsimop->arguments[0].v.var_val &&
			dsimop->arguments[0].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
			dsimop->arguments[0].v.var_val->data) {
		guint netid = sim_master_dsimop_helper(dsimop, master->network_tracking, dsim_vartracker_type_nettrack);
		guint cdf_id = ((sim_master_tracker_tp)dsimop->arguments[0].v.var_val->data)->id;
		gdouble reliability = dsimop->arguments[1].v.gdouble_val;

		/* get the cdf used for latency */
		sim_master_tracker_tp cdf_tracker = g_hash_table_lookup(master->cdf_tracking, &cdf_id);
		if(cdf_tracker != NULL && cdf_tracker->value != NULL) {
			/* add it to our topology */
			cdf_tp cdf = cdf_tracker->value;
			simnet_graph_add_vertex(master->network_topology, netid, cdf, reliability);
		}
	}
}

static void sim_master_dsimop_connect_networks(sim_master_tp master, operation_tp dsimop) {
	debugf("SMaster: Parsing DSIM Operation: connect_networks()\n");

	nbdf_tp nb_op;

	/* make sure we have the dsim variable data */
	if(dsimop->arguments[0].v.var_val &&
			dsimop->arguments[0].v.var_val->data_type == dsim_vartracker_type_nettrack &&
			dsimop->arguments[0].v.var_val->data &&
			dsimop->arguments[1].v.var_val &&
			dsimop->arguments[1].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
			dsimop->arguments[1].v.var_val->data &&
			dsimop->arguments[3].v.var_val &&
			dsimop->arguments[3].v.var_val->data_type == dsim_vartracker_type_nettrack &&
			dsimop->arguments[3].v.var_val->data &&
			dsimop->arguments[4].v.var_val &&
			dsimop->arguments[4].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
			dsimop->arguments[4].v.var_val->data) {

		guint net1_id = ((sim_master_tracker_tp)dsimop->arguments[0].v.var_val->data)->id;
		guint cdf_id_latency_net1_to_net2 = ((sim_master_tracker_tp)dsimop->arguments[1].v.var_val->data)->id;
		gdouble reliability_net1_to_net2 = dsimop->arguments[2].v.gdouble_val;
		guint net2_id = ((sim_master_tracker_tp)dsimop->arguments[3].v.var_val->data)->id;
		guint cdf_id_latency_net2_to_net1 = ((sim_master_tracker_tp)dsimop->arguments[4].v.var_val->data)->id;
		gdouble reliability_net2_to_net1 = dsimop->arguments[5].v.gdouble_val;


		/* encode the simop to NBDF */
		nb_op = simop_nbdf_encode(dsimop, 0);

		/* notify all the workers about the new connection */
		dvn_packet_route(DVNPACKET_GLOBAL_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_OP, nb_op);

		nbdf_free(nb_op);

		/* get the cdfs used for latency */
		sim_master_tracker_tp cdf_tracker_1to2 = g_hash_table_lookup(master->cdf_tracking, &cdf_id_latency_net1_to_net2);
		sim_master_tracker_tp cdf_tracker_2to1 = g_hash_table_lookup(master->cdf_tracking, &cdf_id_latency_net2_to_net1);
		if(cdf_tracker_1to2 != NULL && cdf_tracker_1to2->value != NULL
				&& cdf_tracker_2to1 != NULL && cdf_tracker_2to1->value != NULL) {
			/* add it to our topology */
			cdf_tp cdf_1to2 = cdf_tracker_1to2->value;
			cdf_tp cdf_2to1 = cdf_tracker_2to1->value;
			simnet_graph_add_edge(master->network_topology, net1_id, cdf_1to2, reliability_net1_to_net2, net2_id, cdf_2to1, reliability_net2_to_net1);
		}
	}
}

static void sim_master_dsimop_create_hostname(sim_master_tp master, operation_tp dsimop) {
	debugf("SMaster: Parsing DSIM Operation: create_hostname()\n");
	sim_master_dsimop_helper(dsimop, master->base_hostname_tracking, dsim_vartracker_type_basehostnametrack);
}

static void sim_master_dsimop_create_nodes(sim_master_tp master, operation_tp dsimop) {
	debugf("SMaster: Parsing DSIM Operation: create_nodes()\n");

	nbdf_tp nb_op = NULL;

	/* make sure we have the dsim variable data */
	if(dsimop->arguments[1].v.var_val &&
			dsimop->arguments[1].v.var_val->data_type == dsim_vartracker_type_modtrack &&
			dsimop->arguments[1].v.var_val->data &&
			dsimop->arguments[2].v.var_val &&
			dsimop->arguments[2].v.var_val->data_type == dsim_vartracker_type_nettrack &&
			dsimop->arguments[2].v.var_val->data &&
			dsimop->arguments[3].v.var_val &&
			dsimop->arguments[3].v.var_val->data_type == dsim_vartracker_type_basehostnametrack &&
			dsimop->arguments[3].v.var_val->data &&
			dsimop->arguments[6].v.var_val &&
			dsimop->arguments[6].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
			dsimop->arguments[6].v.var_val->data) {

		/* must have one cdf, but the other one can be anything if not a cdf, it will be ignored */
		gint n_cdfs = 0;
		if(dsimop->arguments[4].v.var_val &&
			dsimop->arguments[4].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
			dsimop->arguments[4].v.var_val->data) {
			n_cdfs++;
		}
		if(dsimop->arguments[5].v.var_val &&
			dsimop->arguments[5].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
			dsimop->arguments[5].v.var_val->data) {
			n_cdfs++;
		}

		if(n_cdfs < 1) {
			dlogf(LOG_ERR, "SMaster: Invalid DSIM file submitted. Please use at least one bandwidth cdf for node creation.\n");
			return;
		}

		guint quantity = ((guint)(dsimop->arguments[0].v.gdouble_val));
		sim_master_tracker_tp hostname_tracker = dsimop->arguments[3].v.var_val->data;

		/* multi node creation. split the job up. */
		for(gint i = 0; i < quantity; i++) {
			guint slave_id = i % master->num_slaves;
			nb_op = simop_nbdf_encode(dsimop, hostname_tracker->counter++);
			dvn_packet_route(DVNPACKET_SLAVE, DVNPACKET_LAYER_SIM, slave_id, SIM_FRAME_OP, nb_op);
			nbdf_free(nb_op);
		}
	}
}

static void sim_master_dsimop_end(sim_master_tp master, operation_tp dsimop) {
	debugf("SMaster: Parsing DSIM Operation: end()\n");

	nbdf_tp nb_op = simop_nbdf_encode(dsimop, 0);

	dvn_packet_route(DVNPACKET_GLOBAL_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_OP, nb_op);

	nbdf_free(nb_op);

	master->end_time_found = 1;
}


void sim_master_opexec(sim_master_tp ma, operation_tp op) {
	/* handles all commands from DSIM. encode them to simops, and sends to
	 * the workers as needed so everyone has the same state.
	 */
	switch(op->type){
		case OP_LOAD_PLUGIN: {
			sim_master_dsimop_load_plugin(ma, op);
			break;
		}
		case OP_LOAD_CDF: {
			sim_master_dsimop_load_cdf(ma, op);
			break;
		}
		case OP_GENERATE_CDF: {
			sim_master_dsimop_generate_cdf(ma, op);
			break;
		}
		case OP_CREATE_NETWORK: {
			sim_master_dsimop_create_network(ma, op);
			break;
		}
		case OP_CONNECT_NETWORKS: {
			sim_master_dsimop_connect_networks(ma, op);
			break;
		}
		case OP_CREATE_HOSTNAME: {
			sim_master_dsimop_create_hostname(ma, op);
			break;
		}
		case OP_CREATE_NODES: {
			sim_master_dsimop_create_nodes(ma, op);
			break;
		}
		case OP_END: {
			sim_master_dsimop_end(ma, op);
			break;
		}
		default: {
			dlogf(LOG_ERR, "sim_master_opexec: Unknown dsim operation!? ");
			break;
		}
	}
}
