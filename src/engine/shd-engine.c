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

#include "util/heap.h"
#include "util/btree.h"
#include "util/global.h"
#include "core/evtracker.h"
#include "dsim/dsim_utils.h"

Engine* engine_new(Configuration* config) {
	MAGIC_ASSERT(config);

	Engine* engine = g_new(Engine, 1);
	MAGIC_INIT(engine);

	engine->config = config;

	/* initialize the singleton-per-thread worker class */
	engine->workerKey = g_private_new(worker_free);

	if(config->nWorkerThreads > 0) {
		/* we need some workers, create a thread pool */
		GError *error = NULL;
		engine->workerPool = g_thread_pool_new(worker_executeEvent, engine,
				config->nWorkerThreads, TRUE, &error);
		if (!engine->workerPool) {
			error("thread pool failed: %s\n", error->message);
			g_error_free(error);
		}
	} else {
		/* one thread, use simple queue, no thread pool needed */
		engine->workerPool = NULL;
	}

	/* holds all events if single-threaded, and non-node events otherwise. */
	engine->masterEventQueue = g_async_queue_new_full(event_free);
	engine->workersIdle = g_cond_new();
	engine->engineIdle = g_mutex_new();

	engine->registry = registry_new();
	registry_register(engine->registry, NODES, node_free);

	engine->minTimeJump = config->minTimeJump * SIMTIME_ONE_MILLISECOND;

	return engine;
}

void engine_free(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* only free thread pool if we actually needed one */
	if(engine->workerPool) {
		g_thread_pool_free(engine->workerPool, FALSE, TRUE);
	}

	if(engine->masterEventQueue) {
		g_async_queue_unref(engine->masterEventQueue);
	}

	g_cond_free(engine->workersIdle);
	g_mutex_free(engine->engineIdle);

	registry_free(engine->registry);

	MAGIC_CLEAR(engine);
	g_free(engine);
}

static gint _engine_processEvents(Engine* engine) {
	MAGIC_ASSERT(engine);

	Worker* worker = worker_getPrivate();
	worker->clock_now = SIMTIME_INVALID;
	worker->clock_last = 0;
	worker->cached_engine = engine;

	Event* next_event = g_async_queue_try_pop(engine->masterEventQueue);

	/* process all events in the priority queue */
	while(next_event && (next_event->time < engine->executeWindowEnd) &&
			(next_event->time < engine->endTime))
	{
		/* get next event */
		worker->cached_event = next_event;
		MAGIC_ASSERT(worker->cached_event);

		/* ensure priority */
		worker->clock_now = worker->cached_event->time;
		engine->clock = worker->clock_now;
		g_assert(worker->clock_now >= worker->clock_last);

		event_run(worker->cached_event);
		event_free(worker->cached_event);
		worker->cached_event = NULL;

		worker->clock_last = worker->clock_now;
		worker->clock_now = SIMTIME_INVALID;

		next_event = g_async_queue_try_pop(engine->masterEventQueue);
	}

	/* push the next event in case we didnt execute it */
	if(next_event) {
		engine_pushEvent(engine, next_event);
	}

	return 0;
}

static void _engine_manageExecutableMail(gpointer data, gpointer user_data) {
	Node* node = data;
	Engine* engine = user_data;
	MAGIC_ASSERT(node);
	MAGIC_ASSERT(engine);

	/* pop mail from mailbox, check that its in window, push as a task */
	Event* event = node_popMail(node);
	while(event && (event->time < engine->executeWindowEnd)
			&& (event->time < engine->endTime)) {
		g_assert(event->time >= engine->executeWindowStart);
		node_pushTask(node, event);
		event = node_popMail(node);
	}

	/* if the last event we popped was beyond the allowed execution window,
	 * push it back into mailbox so it gets executed during the next iteration
	 */
	if(event && (event->time >= engine->executeWindowEnd)) {
		node_pushMail(node, event);
	}

	if(node_getNumTasks(node) > 0) {
		/* now let the worker handle all the node's events */
		g_thread_pool_push(engine->workerPool, node, NULL);

		/* we just added another node that must be processed */
		g_atomic_int_inc(&(engine->protect.nNodesToProcess));
	}
}

static gint _engine_distributeEvents(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* process all events in the priority queue */
	while(engine->executeWindowStart < engine->endTime)
	{
		/* set to one, so after adding nodes we can decrement and check
		 * if all nodes are done by checking for 0 */
		g_atomic_int_set(&(engine->protect.nNodesToProcess), 1);

		/*
		 * check all nodes, moving events that are within the execute window
		 * from their mailbox into their priority queue for execution. all
		 * nodes that have executable events are placed in the thread pool and
		 * processed by a worker thread.
		 */
		GList* node_list = registry_getAll(engine->registry, NODES);

		/* after calling this, multiple threads are running */
		g_list_foreach(node_list, _engine_manageExecutableMail, engine);
		g_list_free(node_list);

		/* wait for all workers to process their events. the last worker must
		 * wait until we are actually listening for the signal before he
		 * sends us the signal to prevent deadlock. */
		if(!g_atomic_int_dec_and_test(&(engine->protect.nNodesToProcess))) {
			while(g_atomic_int_get(&(engine->protect.nNodesToProcess)))
				g_cond_wait(engine->workersIdle, engine->engineIdle);
		}

		/* other threads are sleeping */

		/* execute any non-node events
		 * TODO: parallelize this if it becomes a problem. for now I'm assume
		 * that we wont have enough non-node events to matter.
		 * FIXME: this doesnt make sense with actions / events
		 */
		_engine_processEvents(engine);

		/*
		 * finally, update the allowed event execution window.
		 * TODO: should be able to jump to next event time of any node
		 * in case its far in the future
		 */
		engine->executeWindowStart = engine->executeWindowEnd;
		engine->executeWindowEnd += engine->minTimeJump;
		debug("updated execution window [%lu--%lu]",
				engine->executeWindowStart, engine->executeWindowEnd);
	}

	return 0;
}

// XXX: take this out when we actually parse DSIM and get real nodes, etc
static void _addNodeEvents(gpointer data, gpointer user_data) {
	Node* node = data;
	MAGIC_ASSERT(node);

	SpinEvent* se = spine_new(1);
	SimulationTime t = se->spin_seconds * SIMTIME_ONE_SECOND;
	worker_scheduleEvent((Event*)se, t, node->node_id);
}

static gboolean engine_parse_dsim(Engine* engine) {
//	gchar* dsim_filename = engine->config->dsim_filename->str;
//	gchar* dsim_file = file_get_contents(dsim_filename);
//	if(dsim_file == NULL){
//		warning("cant find dsim file at %s", dsim_filename);
//		return FALSE;
//	}
//
//	dsim_tp dsim_parsed = dsim_create(dsim_file);
//
//	operation_tp op;
//	while((op=dsim_get_nextevent(dsim_parsed, NULL, 1)))
//	{
//		switch(op->type){
//			case OP_LOAD_PLUGIN: {
//				debug("OP_LOAD_PLUGIN");
//
//				GString* plugin_filepath = g_string_new(op->arguments[0].v.string_val);
//				gint id = engine_generateModuleID(engine);
//				LoadPluginAction* a = loadplugin_new(id, engine->registry, plugin_filepath);
//
//				break;
//			}
//			case OP_LOAD_CDF: {
//				debug("OP_LOAD_CDF");
//
//				GString* cdf_filepath = g_string_new(op->arguments[0].v.string_val);
//				gint id = engine_generateCDFID(engine);
//				LoadCDFAction* a = loadcdf_new(id, engine->registry, cdf_filepath);
//
//				break;
//			}
//			case OP_GENERATE_CDF: {
//				debug("OP_GENERATE_CDF");
//
//				guint cdf_base_center = (guint)(op->arguments[0].v.gdouble_val);
//				guint cdf_base_width = (guint)(op->arguments[1].v.gdouble_val);
//				guint cdf_tail_width = (guint)(op->arguments[2].v.gdouble_val);
//
//				cdf_tp cdf = cdf_generate(cdf_base_center, cdf_base_width, cdf_tail_width);
//
//				/* normally this would happen at the event exe time */
//				cdf_tp cdf = cdf_generate(op->base_delay, op->base_width, op->tail_width);
//				if(cdf != NULL) {
//					g_hash_table_insert(wo->loaded_cdfs, gint_key(op->id), cdf);
//				}
//				break;
//			}
//			case OP_CREATE_NETWORK: {
//				debug("OP_CREATE_NETWORK");
//
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
//
//				break;
//			}
//			case OP_CONNECT_NETWORKS: {
//				debug("OP_CONNECT_NETWORKS");
//
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
//
//				break;
//			}
//			case OP_CREATE_HOSTNAME: {
//				debug("OP_CREATE_HOSTNAME");
//
//				gchar* base_hostname = op->arguments[0].v.string_val;
//
//				/* normally this would happen at the event exe time */
//
//				gchar* base_hostname = malloc(sizeof(op->base_hostname));
//				strncpy(base_hostname, op->base_hostname, sizeof(op->base_hostname));
//
//				g_hash_table_insert(wo->hostname_tracking, gint_key(op->id), base_hostname);
//
//				break;
//			}
//			case OP_CREATE_NODES: {
//				debug("OP_CREATE_NODES");
//
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
//
//				break;
//			}
//			case OP_END: {
//				debug("OP_END");
//
//				SimulationTime end_time = (SimulationTime) op->target_time;
//
//				/* normally this would happen at the event exe time */
//
////				wo->mode = sim_worker_mode_complete;
//				// actually should set kill time
//				g_atomic_int_inc(&(shadow_engine->protect.isKilled));
//
//				break;
//			}
//			default: {
//				error("Unknown dsim operation!?");
//				break;
//			}
//		}
//	}

	return FALSE;
}

gint engine_run(Engine* engine) {
	MAGIC_ASSERT(engine);

	/* make sure our bootstrap events are set properly */
	Worker* worker = worker_getPrivate();
	worker->clock_now = 0;
	worker->cached_engine = engine;

	/* parse user simulation script, create jobs */
	debug("parsing simulation script");
//	if(engine_parse_dsim(engine) == FALSE) {
//		return -1;
//	}

	// *******************************
	// XXX: take this out when we actually parse DSIM and get real nodes, etc
	// loop through all nodes and add some events for each.
	gint i = 0;
	for(i=0; i < 1000; i++) {
		Node* n = node_new(engine_generateNodeID(engine));
		registry_put(engine->registry, NODES, &(n->node_id), n);
	}
	GList* node_list = registry_getAll(engine->registry, NODES);
	g_list_foreach(node_list, _addNodeEvents, engine);
	g_list_free(node_list);
	// *******************************

	if(engine->config->nWorkerThreads > 0) {
		/* multi threaded, manage the other workers */
		engine->executeWindowStart = 0;
		engine->executeWindowEnd = engine->minTimeJump;
		return _engine_distributeEvents(engine);
	} else {
		/* single threaded, we are the only worker */
		engine->executeWindowStart = 0;
		engine->executeWindowEnd = G_MAXUINT64;
		return _engine_processEvents(engine);
	}
}

void engine_pushEvent(Engine* engine, Event* event) {
	MAGIC_ASSERT(engine);
	MAGIC_ASSERT(event);
	g_async_queue_push_sorted(engine->masterEventQueue, event, event_compare, NULL);
}

gpointer engine_lookup(Engine* engine, EngineStorage type, gint id) {
	MAGIC_ASSERT(engine);

	/*
	 * Return the item corresponding to type and id in a thread-safe way.
	 * I believe for now no protections are necessary since our registry
	 * is read-only.
	 */
	return registry_get(engine->registry, type, &id);
}

gint engine_generateWorkerID(Engine* engine) {
	MAGIC_ASSERT(engine);
	return g_atomic_int_exchange_and_add(&(engine->protect.workerIDCounter), 1);
}

gint engine_generateNodeID(Engine* engine) {
	MAGIC_ASSERT(engine);
	return g_atomic_int_exchange_and_add(&(engine->protect.nodeIDCounter), 1);
}

gint engine_generateNetworkID(Engine* engine) {
	MAGIC_ASSERT(engine);
	return g_atomic_int_exchange_and_add(&(engine->protect.networkIDCounter), 1);
}

gint engine_generateCDFID(Engine* engine) {
	MAGIC_ASSERT(engine);
	return g_atomic_int_exchange_and_add(&(engine->protect.cdfIDCounter), 1);
}

gint engine_generateModuleID(Engine* engine) {
	MAGIC_ASSERT(engine);
	return g_atomic_int_exchange_and_add(&(engine->protect.moduleIDCounter), 1);
}

gint engine_getNumThreads(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->config->nWorkerThreads;
}

SimulationTime engine_getMinTimeJump(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->minTimeJump;
}

SimulationTime engine_getExecutionBarrier(Engine* engine) {
	MAGIC_ASSERT(engine);
	return engine->executeWindowEnd;
}

void engine_notifyNodeProcessed(Engine* engine) {
	MAGIC_ASSERT(engine);

	/*
	 * if all the nodes have been processed and the engine is done adding nodes,
	 * nNodesToProcess could be 0. if it is, get the engine lock to ensure it
	 * is listening for the signal, then signal the condition.
	 */
	if(g_atomic_int_dec_and_test(&(engine->protect.nNodesToProcess))) {
		g_mutex_lock(engine->engineIdle);
		g_cond_signal(engine->workersIdle);
		g_mutex_unlock(engine->engineIdle);
	}
}
