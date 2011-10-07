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

RunnableVTable createnodes_vtable = { (RunnableRunFunc) createnodes_run,
		(RunnableFreeFunc) createnodes_free, MAGIC_VALUE };

CreateNodesAction* createnodes_new(guint64 quantity, GString* name,
		GString* applicationName, GString* cpudelayCDFName,
		GString* networkName, GString* bandwidthupCDFName,
		GString* bandwidthdownCDFName)
{
	g_assert(name && applicationName && cpudelayCDFName && networkName && bandwidthupCDFName && bandwidthdownCDFName);
	CreateNodesAction* action = g_new0(CreateNodesAction, 1);
	MAGIC_INIT(action);

	action_init(&(action->super), &createnodes_vtable);

	action->quantity = quantity;
	action->name = g_string_new(name->str);
	action->applicationName = g_string_new(applicationName->str);
	action->cpudelayCDFName = g_string_new(cpudelayCDFName->str);
	action->networkName = g_string_new(networkName->str);
	action->bandwidthup = g_string_new(bandwidthupCDFName->str);
	action->bandwidthdown = g_string_new(bandwidthdownCDFName->str);

	return action;
}

void createnodes_run(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

//				/* make sure we have the dsim variable data */
//				if(dsimop->arguments[1].v.var_val &&
//						dsimop->arguments[1].v.var_val->data_type == dsim_vartracker_type_modtrack &&
//						dsimop->arguments[1].v.var_val->data &&
//						dsimop->arguments[2].v.var_val &&
//						dsimop->arguments[2].v.var_val->data_type == dsim_vartracker_type_nettrack &&
//						dsimop->arguments[2].v.var_val->data &&
//						dsimop->arguments[3].v.var_val &&
//						dsimop->arguments[3].v.var_val->data_type == dsim_vartracker_type_basehostnametrack &&
//						dsimop->arguments[3].v.var_val->data &&
//						dsimop->arguments[6].v.var_val &&
//						dsimop->arguments[6].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
//						dsimop->arguments[6].v.var_val->data) {
//
//					/* must have one cdf, but the other one can be anything if not a cdf, it will be ignored */
//					gint n_cdfs = 0;
//					if(dsimop->arguments[4].v.var_val &&
//						dsimop->arguments[4].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
//						dsimop->arguments[4].v.var_val->data) {
//						n_cdfs++;
//					}
//					if(dsimop->arguments[5].v.var_val &&
//						dsimop->arguments[5].v.var_val->data_type == dsim_vartracker_type_cdftrack &&
//						dsimop->arguments[5].v.var_val->data) {
//						n_cdfs++;
//					}
//
//					if(n_cdfs < 1) {
//						dlogf(LOG_ERR, "SMaster: Invalid DSIM file submitted. Please use at least one bandwidth cdf for node creation.\n");
//						return;
//					}
//
//					guint quantity = ((guint)(dsimop->arguments[0].v.gdouble_val));
//					sim_master_tracker_tp hostname_tracker = dsimop->arguments[3].v.var_val->data;
//
//					/* multi node creation. split the job up. */
//					for(gint i = 0; i < quantity; i++) {
//						guint slave_id = i % master->num_slaves;
//						nb_op = simop_nbdf_encode(dsimop, hostname_tracker->counter++);
//						dvn_packet_route(DVNPACKET_SLAVE, DVNPACKET_LAYER_SIM, slave_id, SIM_FRAME_OP, nb_op);
//						nbdf_free(nb_op);
//					}
//				}
//
//				/* normally this would happen at the event exe time */
//
//				module_tp  module = module_get_module(wo->mod_mgr, op->plugin_id);
//
//				if(!module)
//					return 1;
//
//				debugf("SWorker (%d): Spawning node @%llu.\n", wo->process_id, sop->target_time);
//
//				/* every node contains its own context */
//				context_provider_tp context_provider = malloc(sizeof(context_provider_t));
//
//				if(!context_provider)
//					printfault(EXIT_NOMEM, "sim_worker_opexec_cnodes: Out of memory");
//
//				/* assign an IP and start tracking */
//				in_addr_t addr = vci_create_ip(wo->vci_mgr, op->network_id, context_provider);
//
//				if(addr == INADDR_NONE) {
//					dlogf(LOG_ERR, "SWorker: Failure to create ip. cant instantiate node!\n");
//					return 1;
//				}
//
//				/* get bandwidth */
//				guint32 KBps_up = 0;
//				guint32 KBps_down = 0;
//
//				/* if we only have 1 id, we have symmetric bandwidth, otherwise asym as specified by cdfs */
//				if(op->cdf_id_bandwidth_up == 0 || op->cdf_id_bandwidth_down == 0) {
//					guint sym_id = op->cdf_id_bandwidth_up != 0 ? op->cdf_id_bandwidth_up : op->cdf_id_bandwidth_down;
//					cdf_tp sym = g_hash_table_lookup(wo->loaded_cdfs, &sym_id);
//					KBps_up = KBps_down = (guint32)cdf_random_value(sym);
//				} else {
//					cdf_tp up = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_bandwidth_up);
//					cdf_tp down = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_bandwidth_down);
//					KBps_up = (guint32)cdf_random_value(up);
//					KBps_down = (guint32)cdf_random_value(down);
//				}
//
//				/* create unique hostname. master tells us what id we should prepend to ensure uniqueness */
//				gchar* basename = g_hash_table_lookup(wo->hostname_tracking, &op->hostname_id);
//				if(basename == NULL) {
//					dlogf(LOG_ERR, "SWorker: Failure to create hostname. cant instantiate node!\n");
//					return 1;
//				}
//
//				gchar hostname[SIMOP_STRING_LEN];
//				if(op->hostname_unique_counter == 0) {
//					snprintf(hostname, SIMOP_STRING_LEN, "%s", basename);
//				} else {
//					snprintf(hostname, SIMOP_STRING_LEN, "%u.%s", op->hostname_unique_counter, basename);
//				}
//
//				/* add this nodes hostname, etc,  to resolver map */
//				resolver_add(wo->resolver, hostname, addr, 0, KBps_down, KBps_up);
//
//
//				cdf_tp cpu_speed_cdf = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_cpu_speed);
//				guint64 cpu_speed_Bps = (guint64)cdf_random_value(cpu_speed_cdf);
//
//				/* create vnetwork management */
//				context_provider->vsocket_mgr = vsocket_mgr_create(context_provider, addr, KBps_down, KBps_up, cpu_speed_Bps);
//
//				/* allocate module memory */
//				context_provider->modinst = module_create_instance(module, addr);
//
//				/* setup node logging channel */
//				context_provider->log_channel = 0;
//				context_provider->log_level = 1;
//
//				/* FIXME the following mappings never get removed, but probably should when
//				 * sim_worker_destroy_node is called.
//				 * this means removing from resolver and sending out to all other workers
//				 * so they can too. */
//
//				/* other workers track the node's network membership and name-addr map */
//				nbdf_tp vci_net_track = nbdf_construct("iasii", op->network_id, addr, hostname,
//						KBps_down, KBps_up);
//				dvn_packet_route(DVNPACKET_GLOBAL_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_TRACK, vci_net_track);
//				nbdf_free(vci_net_track);
//
//				/* parse the cl_args ginto separate strings */
//				GQueue *args = g_queue_new();
//				gchar* result = strtok(op->cl_args, " ");
//				while(result != NULL) {
//					g_queue_push_tail(args, result);
//					result = strtok(NULL, " ");
//				}
//
//				/* setup for instantiation */
//				gint argc = g_queue_get_length(args);
//				gchar* argv[argc];
//				gint argi = 0;
//				for(argi = 0; argi < argc; argi++) {
//					argv[argi] = g_queue_pop_head(args);
//				}
//				g_queue_free(args);
//
//				dlogf(LOG_MSG, "SWorker: Instantiating node, ip %s, hostname %s, upstream %u KBps, downstream %u KBps\n", inet_ntoa_t(addr), hostname, KBps_up, KBps_down);
//
//				/* call module instantiation */
//				context_execute_instantiate(context_provider, argc, argv);
}

void createnodes_free(CreateNodesAction* action) {
	MAGIC_ASSERT(action);

	g_string_free(action->name, TRUE);
	g_string_free(action->applicationName, TRUE);
	g_string_free(action->cpudelayCDFName, TRUE);
	g_string_free(action->networkName, TRUE);
	g_string_free(action->bandwidthdown, TRUE);
	g_string_free(action->bandwidthup, TRUE);

	MAGIC_CLEAR(action);
	g_free(action);
}
