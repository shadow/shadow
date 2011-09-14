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
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <dlfcn.h>

#include "global.h"
#include "sysconfig.h"
#include "sim.h"
#include "events.h"
#include "netconst.h"
#include "routing.h"
#include "simop.h"
#include "timer.h"
#include "utility.h"
#include "context.h"
#include "vsocket_mgr.h"
#include "resolver.h"
#include "shd-cdf.h"

/* TODO so many NULL checks are missing all over this code */

typedef gint (*gettimeofday_fp)(struct timeval*, __timezone_ptr_t*);

sim_worker_tp sim_worker_create (pipecloud_tp pipecloud, gint slave_id, gint process_id, guint num_slaves, guint num_workers, guint max_wrkrs_per_slave) {
	sim_worker_tp rv;

	rv=malloc(sizeof(*rv));

	rv->pipecloud = pipecloud;
	rv->events = events_create();
	rv->stalled_simops = g_queue_new();
	rv->mod_mgr = module_mgr_create();

	rv->hostname_tracking = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, g_free);
	rv->loaded_cdfs = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, NULL);

	rv->timer_mgr = dtimer_create_manager(rv->events);
	rv->ascheme = vci_create_addressing_scheme(num_slaves, max_wrkrs_per_slave);
	rv->vci_mgr = vci_mgr_create(rv->events, slave_id, process_id, rv->ascheme);
	rv->resolver = resolver_create(process_id);
	rv->network_topology = simnet_graph_create();

	rv->num_workers = num_workers;
	rv->process_id = process_id;
	rv->max_latency = rv->min_latency = 0;
	rv->worker_states = malloc(sizeof(*rv->worker_states) * (num_workers + 1));
	rv->current_time = PTIME_INVALID;
	rv->last_broadcast = PTIME_INVALID;

	rv->destroying = 0;

	/* startup timestamp */
	clock_gettime(CLOCK_MONOTONIC, &rv->wall_time_at_startup);

	for(gint i=0; i <= num_workers; i++) {
		rv->worker_states[i].window = PTIME_INVALID;
		rv->worker_states[i].last_event = PTIME_INVALID;
		rv->worker_states[i].next_event = PTIME_INVALID;
		rv->worker_states[i].current = PTIME_INVALID;
		rv->worker_states[i].valid = 0;
	}

	rv->my_state = &rv->worker_states[process_id];
	rv->my_state->valid = 1;

	rv->mode = sim_worker_mode_spool;

	return rv;
}

static void sim_worker_schedule_ticktock(sim_worker_tp worker) {
	ticktock_event_tp tt = malloc(sizeof(ticktock_event_t));
	tt->time_scheduled = worker->current_time;
	events_schedule(worker->events, worker->current_time + TICKTOCK_INTERVAL, tt, EVENTS_TYPE_TICKTOCK);
}

static void sim_worker_handle_ticktock(sim_worker_tp worker, gpointer event) {
	ticktock_event_tp tt = event;

	/* CLOCK_REALTIME is intercepted... */
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	unsigned long long now_millis = (unsigned long long)((now.tv_sec - worker->wall_time_at_startup.tv_sec) * 1000);
	now_millis += ((now.tv_nsec -  worker->wall_time_at_startup.tv_nsec)/ 1000000);

	unsigned long long tick = (unsigned long long) tt->time_scheduled;
	unsigned long long tock = (unsigned long long) worker->current_time;
	dlogf(LOG_MSG, "TICKTOCK: %llu wall milliseconds and %llu sim milliseconds (%llu-->%llu)\n", now_millis, tock, tick, tock);

	/* schedule another */
	sim_worker_schedule_ticktock(worker);
	free(tt);
}

void sim_worker_deposit(sim_worker_tp worker, gint frametype, nbdf_tp frame) {
	switch(frametype) {
		case SIM_FRAME_START: {
			nbdf_read(frame, "ii", &worker->max_latency, &worker->min_latency);
			worker->mode = sim_worker_mode_simulating;
			sim_worker_schedule_ticktock(worker);
			break;
		}

		case SIM_FRAME_OP: {
			simop_tp sop = simop_nbdf_decode(frame);
			if(sop)
				events_schedule(worker->events, sop->target_time, sop, EVENTS_TYPE_SIMOP);
			break;
		}

		case SIM_FRAME_TRACK: {
			guint network_id;
			in_addr_t addr;
			gchar* hostname;
			guint32 KBps_down;
			guint32 KBps_up;

			nbdf_read(frame, "iaSii", &network_id, &addr, &hostname, &KBps_down, &KBps_up);

			debugf("SWorker (%d): Tracking node ip: %s in network %u\n", worker->process_id, inet_ntoa_t(addr), network_id);
			vci_track_network(worker->vci_mgr, network_id, addr);

			debugf("SWorker (%d): Creating ip:hostname mapping %s:%s\n", worker->process_id, inet_ntoa_t(addr), hostname);
			/* at this point the unique id will have already been prepended if needed */
			resolver_add(worker->resolver, hostname, addr, 0, KBps_down, KBps_up);

			break;
		}

		case SIM_FRAME_VCI_PACKET_NOPAYLOAD:
		case SIM_FRAME_VCI_PACKET_PAYLOAD:
		case SIM_FRAME_VCI_PACKET_NOPAYLOAD_SHMCABINET:
		case SIM_FRAME_VCI_PACKET_PAYLOAD_SHMCABINET:
		case SIM_FRAME_VCI_RETRANSMIT:
		case SIM_FRAME_VCI_CLOSE: {
			vci_deposit(worker->vci_mgr, frame, frametype);
			break;
		}

		case SIM_FRAME_STATE: {
			guint source_worker_id;
			ptime_t last, current, next, window;

			nbdf_read(frame, "itttt", &source_worker_id, &last, &current, &next, &window);

			sim_worker_remotestate_tp remote = &worker->worker_states[source_worker_id];
			remote->last_event = last;
			remote->current = current;
			remote->next_event = next;
			remote->window = window;
			remote->valid = 1;

			debugf("SWorker (%d): Got state from %d: last @%llu current @%llu next @%llu window @%llu\n",
					worker->process_id, source_worker_id, last, current, next, window);
		}
	}
}

ptime_t sim_worker_calcwindow(sim_worker_tp worker) {
	ptime_t min_next = PTIME_MAX;
	ptime_t min_last = PTIME_MAX;
	ptime_t min_window = PTIME_MAX;
	ptime_t max_window = PTIME_INVALID;
	ptime_t min_current = PTIME_MAX;

	if(g_queue_get_length(worker->stalled_simops) > 0) {
		debugf("Stalled for simop wait!\n");
		return PTIME_INVALID;
	}

	/* if I am the only worker, I can run forever */
	if(worker->num_workers == 1) {
		return PTIME_MAX;
	}

	for(gint i=0; i <= worker->num_workers; i++) {
		/* dont count master or my own events */
		if(i == 0 || i == worker->process_id) {
			continue;
		}

		sim_worker_remotestate_tp state = &worker->worker_states[i];

		if(!state->valid) {
			debugf("SWorker (%d):  Break, not enough state information to proceed.\n", worker->process_id);
			return PTIME_INVALID;
		}

		if(state->last_event < min_last) {
			min_last = state->last_event;
		}

		if(state->next_event < min_next) {
			min_next = state->next_event;
		}

		if(state->window < min_window) {
			min_window = state->window;
		}

		if(state->window > max_window) {
			max_window = state->window;
		}

		if(state->current < min_current) {
			min_current = state->current;
		}
	}

	/* we can always run ahead out to our minimum latency, but may be able to go further. */
	ptime_t window = min_last + worker->min_latency - 1;

	/* we can run until any other message would arrive from another worker.
	 * ! be careful with min_next, because a worker could receive another event that
	 * reduces its next event time but not tell us about it until later. this means
	 * min_next is not really accurate. therefore we use min_current here,
	 * which is every workers view of the current time. min_current is the
	 * conservative and safe version of min_next (min_next bounded by the window barrier) */
	ptime_t earliest_possible_event = min_current + worker->min_latency - 1;

//	/* I might affect the earliest event */
//	if(worker->my_state->next_event < earliest_possible_event) {
//		/* my next event could cause another worker to send me an event earlier
//		 * than its current next event */
//		ptime_t earliest_affect_myself = worker->my_state->next_event + (2 * worker->min_latency) - 2;
//		if(earliest_affect_myself < earliest_possible_event) {
//			earliest_possible_event = earliest_affect_myself;
//		}
//	}

	if(earliest_possible_event > window) {
		window = earliest_possible_event;
	}

	/* if we've already executed an event past the time any other event
	 * could possibly arrive, then we can go to the next event. */
//	ptime_t latest_possible_event = max_window + worker->max_latency - 1;
//	if(worker->my_state->last_event > latest_possible_event) {
//		window = min_next + worker->min_latency - 1;
//	}

	return window;
}

static ptime_t sim_worker_advance_remote_workers(sim_worker_tp worker) {
	ptime_t min_time_affect_others = worker->my_state->current + worker->min_latency - 1;

	/* returns true if the time that my next event will affect others can push ahead their
	 * window */
	for(gint i=0; i <= worker->num_workers; i++) {
		/* dont count master or my own events */
		if(i == 0 || i == worker->process_id) {
			continue;
		}

		sim_worker_remotestate_tp state = &worker->worker_states[i];

		if(!state->valid && state->last_event != PTIME_INVALID) {
			break;
		}

		if(min_time_affect_others > state->window) {
			return min_time_affect_others;
		}
	}

	return PTIME_INVALID;
}

static void sim_worker_sync_time(sim_worker_tp worker) {
	if(g_queue_get_length(worker->stalled_simops) > 0) {
		dlogf(LOG_WARN, "sim_worker_sync_time: stalled simops!!\n");
		return;
	}

	/* always update next event and last event times.
	 * --at the beginning of the sim, current_time will be PTIME_INVALID.
	 * --when the event queue is empty, next event time will be PTIME_INVALID. */
	worker->my_state->last_event = worker->current_time;

	/* the next event time is not accurate until I read all frames from pipecloud */
	worker->my_state->next_event = events_get_next_time(worker->events);

	ptime_t window = sim_worker_calcwindow(worker);
	if(window != PTIME_INVALID) {
		worker->my_state->window = window;
	}

	/* set my barrier as my view of the current time */
	if(worker->my_state->next_event > worker->my_state->window) {
		worker->my_state->current = worker->my_state->window;
	} else {
		worker->my_state->current = worker->my_state->next_event;
	}

	ptime_t advance_to = sim_worker_advance_remote_workers(worker);
	if(worker->last_broadcast < advance_to) {
		/* we have not broadcasted this change yet, so
		 * broadcast our new state to advance other workers' windows */
		nbdf_tp state_frame = nbdf_construct("itttt", worker->process_id,
			worker->my_state->last_event, worker->my_state->current,
			worker->my_state->next_event, worker->my_state->window);

		dvn_packet_route(DVNPACKET_WORKER_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_STATE, state_frame);
		nbdf_free(state_frame);

		worker->last_broadcast = advance_to;

		debugf("SWorker (%d): State broadcasted: last @%llu current @%llu next @%llu window @%llu\n",
			worker->process_id, worker->my_state->last_event, worker->my_state->current,
			worker->my_state->next_event, worker->my_state->window);
	}
}

gint sim_worker_heartbeat(sim_worker_tp worker, size_t* num_event_worker_executed) {
	ptime_t event_at;
	gint event_type;
	gpointer event;
	simop_tp simop_event;
	size_t num_executed = 0;

	/* -1 for error, 0 for blocked, 1 for success. */
	gint returnval = 0;

	debugf("SWorker (%d): Heartbeat\n", worker->process_id);

	/* we can't do anything before we've gotten the start packet */
	if(worker->mode != sim_worker_mode_simulating) {
		returnval = 0;
		goto ret;
	}

	/* first, attempt to process any stalled simop events. */
	while((simop_event = g_queue_pop_head(worker->stalled_simops)) != NULL) {
		/* set the current time for SNRI callbacks */
		worker->current_time = simop_event->target_time;

		if(!sim_worker_opexec(worker, simop_event)) {
			g_queue_push_head(worker->stalled_simops, simop_event);
			break;
		}
	}

	sim_worker_sync_time(worker);

	/* while in bounds, dequeue and process! */
	while(events_get_next_time(worker->events) <= worker->my_state->window) {
		event = events_dequeue(worker->events, &event_at, &event_type);

		if(!event)
			break;

		/* gdouble check no backwards time */
		if(event_at < worker->current_time) {
			dlogf(LOG_ERR, "sim_worker_heartbeat: attempting to execute past event type %i scheduled for %llu. killing myself!\n", event_type, event_at);
			returnval = -1;
			goto ret;
		}

		/* jump to our next event. our current window lets us do this safely. */
		worker->current_time = event_at;

		/* pawn off events to event handlers */
		switch(event_type) {
			case EVENTS_TYPE_DTIMER: {
				dtimer_exec_event(worker->timer_mgr, event);
				break;
			}

			case EVENTS_TYPE_VCI: {
				vci_exec_event(worker->vci_mgr, event);
				break;
			}

			case EVENTS_TYPE_SIMOP: {
				if(!sim_worker_opexec(worker, event)) {
					g_queue_push_tail(worker->stalled_simops, event);
					continue;
				}
				break;
			}

			case EVENTS_TYPE_TICKTOCK: {
				sim_worker_handle_ticktock(worker, event);
				break;
			}

			default: {
				dlogf(LOG_ERR, "sim_worker_heartbeat: unknown event type. killing myself!\n");
				returnval = -1;
				goto ret;
			}
		}

		num_executed++;

		if(worker->mode != sim_worker_mode_simulating)
			break;
	}

	/* some of our state has changed, but we dont know our next event until we read all from pipecloud */

	if(worker->mode != sim_worker_mode_simulating)
		returnval = 0;
	else
		returnval = 1;

ret:
	if(num_event_worker_executed != NULL) {
		*num_event_worker_executed = num_executed;
	}
	return returnval;
}

static void sim_worker_destroy_cdftracker_cb(gpointer key, gpointer value, gpointer param) {
	cdf_destroy((cdf_tp)value);
}

void sim_worker_destroy(sim_worker_tp sim) {
	sim->destroying = 1;
	vci_mgr_destroy(sim->vci_mgr);
	sim->vci_mgr = NULL;
	vci_destroy_addressing_scheme(sim->ascheme);
	sim->ascheme = NULL;
	dtimer_destroy_manager(sim->timer_mgr);
	sim->timer_mgr = NULL;
	module_mgr_destroy(sim->mod_mgr);
	sim->mod_mgr = NULL;
	events_destroy(sim->events);
	sim->events = NULL;
	resolver_destroy(sim->resolver);
	sim->resolver = NULL;
	simnet_graph_destroy(sim->network_topology);
	sim->network_topology = NULL;

	/* todo cleanup - use different func */
	g_hash_table_remove_all(sim->hostname_tracking);
	g_hash_table_destroy(sim->hostname_tracking);
	sim->hostname_tracking = NULL;

	g_hash_table_foreach(sim->loaded_cdfs, sim_worker_destroy_cdftracker_cb, NULL);
	g_hash_table_destroy(sim->loaded_cdfs);
	g_hash_table_destroy(sim->loaded_cdfs);
	sim->loaded_cdfs = NULL;

	while(g_queue_get_length(sim->stalled_simops) > 0) {
		simop_destroy(g_queue_pop_head(sim->stalled_simops));
	}
	g_queue_free(sim->stalled_simops);
	sim->stalled_simops = NULL;

	free(sim->worker_states);
	sim->worker_states = NULL;

	free(sim);
	context_set_worker(NULL);
}

sim_worker_nodetracker_tp sim_worker_create_nodetracker(in_addr_t addr, gint track_id, gchar valid) {
	sim_worker_nodetracker_tp nt = malloc(sizeof(sim_worker_nodetracker_t));
	if(!nt)
		printfault(EXIT_NOMEM, "sim_worker_create_nodetracker: Out of memory");

	nt->addr = addr;
	nt->track_id = track_id;
	nt->valid = valid;

	return nt;
}

void sim_worker_destroy_nodetracker_cb(gint key, gpointer value, gpointer param) {
	sim_worker_destroy_nodetracker((sim_worker_nodetracker_tp) value);
}

void sim_worker_destroy_nodetracker(sim_worker_nodetracker_tp nt) {
	free(nt);
}

static gint sim_worker_opexec_load_plugin(sim_worker_tp wo, simop_tp sop) {
	simop_load_plugin_tp op = sop->operation;
	debugf("SWorker (%d): Loading Module: %s\n", wo->process_id, op->filepath);

	module_tp mod = module_load(wo->mod_mgr, op->id, op->filepath);

	if(mod != NULL) {
		context_execute_init(mod);
	} else {
		gchar buffer[200];

		snprintf(buffer,200,"Unable to load and validate '%s'", op->filepath);
		sim_worker_abortsim(wo, buffer);
	}

	return 1;
}

static gint sim_worker_opexec_load_cdf(sim_worker_tp wo, simop_tp sop) {
	simop_load_cdf_tp op = sop->operation;

	cdf_tp cdf = cdf_create(op->filepath);
	if(cdf != NULL) {
		g_hash_table_insert(wo->loaded_cdfs, gint_key(op->id), cdf);
	}

	return 1;
}

static gint sim_worker_opexec_generate_cdf(sim_worker_tp wo, simop_tp sop) {
	simop_generate_cdf_tp op = sop->operation;

	cdf_tp cdf = cdf_generate(op->base_delay, op->base_width, op->tail_width);
	if(cdf != NULL) {
		g_hash_table_insert(wo->loaded_cdfs, gint_key(op->id), cdf);
	}

	return 1;
}

static gint sim_worker_opexec_create_network(sim_worker_tp wo, simop_tp sop) {
	simop_create_network_tp op = sop->operation;

	/* build up our knowledge of the network */
	cdf_tp cdf = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_gintra_latency);
	simnet_graph_add_vertex(wo->network_topology, op->id, cdf, op->reliability);

	/* vci needs ids to look up graph properties */
	vci_network_create(wo->vci_mgr, op->id);

	return 1;
}

static gint sim_worker_opexec_connect_network(sim_worker_tp wo, simop_tp sop) {
	simop_connect_networks_tp op = sop->operation;

	/* build up our knowledge of the network */
	cdf_tp cdf_latency_1to2 = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_latency_1to2);
	cdf_tp cdf_latency_2to1 = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_latency_2to1);

	simnet_graph_add_edge(wo->network_topology, op->network1_id, cdf_latency_1to2, op->reliability_1to2,
			op->network2_id, cdf_latency_2to1, op->reliability_2to1);

	return 1;
}

static gint sim_worker_opexec_create_hostname(sim_worker_tp wo, simop_tp sop) {
	simop_create_hostname_tp op = sop->operation;

	gchar* base_hostname = malloc(sizeof(op->base_hostname));
	strncpy(base_hostname, op->base_hostname, sizeof(op->base_hostname));

	g_hash_table_insert(wo->hostname_tracking, gint_key(op->id), base_hostname);
	return 1;
}

static gint sim_worker_opexec_create_nodes(sim_worker_tp wo, simop_tp sop) {
	simop_create_nodes_tp op = sop->operation;
	module_tp  module = module_get_module(wo->mod_mgr, op->plugin_id);

	if(!module)
		return 1;

	debugf("SWorker (%d): Spawning node @%llu.\n", wo->process_id, sop->target_time);

	/* every node contains its own context */
	context_provider_tp context_provider = malloc(sizeof(context_provider_t));

	if(!context_provider)
		printfault(EXIT_NOMEM, "sim_worker_opexec_cnodes: Out of memory");

	/* assign an IP and start tracking */
	in_addr_t addr = vci_create_ip(wo->vci_mgr, op->network_id, context_provider);

	if(addr == INADDR_NONE) {
		dlogf(LOG_ERR, "SWorker: Failure to create ip. cant instantiate node!\n");
		return 1;
	}

	/* get bandwidth */
	guint32 KBps_up = 0;
	guint32 KBps_down = 0;

	/* if we only have 1 id, we have symmetric bandwidth, otherwise asym as specified by cdfs */
	if(op->cdf_id_bandwidth_up == 0 || op->cdf_id_bandwidth_down == 0) {
		guint sym_id = op->cdf_id_bandwidth_up != 0 ? op->cdf_id_bandwidth_up : op->cdf_id_bandwidth_down;
		cdf_tp sym = g_hash_table_lookup(wo->loaded_cdfs, &sym_id);
		KBps_up = KBps_down = (guint32)cdf_random_value(sym);
	} else {
		cdf_tp up = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_bandwidth_up);
		cdf_tp down = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_bandwidth_down);
		KBps_up = (guint32)cdf_random_value(up);
		KBps_down = (guint32)cdf_random_value(down);
	}

	/* create unique hostname. master tells us what id we should prepend to ensure uniqueness */
	gchar* basename = g_hash_table_lookup(wo->hostname_tracking, &op->hostname_id);
	if(basename == NULL) {
		dlogf(LOG_ERR, "SWorker: Failure to create hostname. cant instantiate node!\n");
		return 1;
	}

	gchar hostname[SIMOP_STRING_LEN];
	if(op->hostname_unique_counter == 0) {
		snprintf(hostname, SIMOP_STRING_LEN, "%s", basename);
	} else {
		snprintf(hostname, SIMOP_STRING_LEN, "%u.%s", op->hostname_unique_counter, basename);
	}

	/* add this nodes hostname, etc,  to resolver map */
	resolver_add(wo->resolver, hostname, addr, 0, KBps_down, KBps_up);


	cdf_tp cpu_speed_cdf = g_hash_table_lookup(wo->loaded_cdfs, &op->cdf_id_cpu_speed);
	guint64 cpu_speed_Bps = (guint64)cdf_random_value(cpu_speed_cdf);

	/* create vnetwork management */
	context_provider->vsocket_mgr = vsocket_mgr_create(context_provider, addr, KBps_down, KBps_up, cpu_speed_Bps);

	/* allocate module memory */
	context_provider->modinst = module_create_instance(module, addr);

	/* setup node logging channel */
	context_provider->log_channel = 0;
	context_provider->log_level = 1;

	/* FIXME the following mappings never get removed, but probably should when
	 * sim_worker_destroy_node is called.
	 * this means removing from resolver and sending out to all other workers
	 * so they can too. */

	/* other workers track the node's network membership and name-addr map */
	nbdf_tp vci_net_track = nbdf_construct("iasii", op->network_id, addr, hostname,
			KBps_down, KBps_up);
	dvn_packet_route(DVNPACKET_GLOBAL_BCAST, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_TRACK, vci_net_track);
	nbdf_free(vci_net_track);

	/* parse the cl_args ginto separate strings */
	GQueue *args = g_queue_new();
	gchar* result = strtok(op->cl_args, " ");
	while(result != NULL) {
		g_queue_push_tail(args, result);
		result = strtok(NULL, " ");
	}

	/* setup for instantiation */
	gint argc = g_queue_get_length(args);
	gchar* argv[argc];
	gint argi = 0;
	for(argi = 0; argi < argc; argi++) {
		argv[argi] = g_queue_pop_head(args);
	}
	g_queue_free(args);

	dlogf(LOG_MSG, "SWorker: Instantiating node, ip %s, hostname %s, upstream %u KBps, downstream %u KBps\n", inet_ntoa_t(addr), hostname, KBps_up, KBps_down);

	/* call module instantiation */
	context_execute_instantiate(context_provider, argc, argv);

	return 1;
}

static gint sim_worker_opexec_end(sim_worker_tp wo, simop_tp sop) {
	nbdf_tp complete_frame;

	complete_frame = nbdf_construct("i", wo->process_id);
	dvn_packet_route(DVNPACKET_LOCAL_SLAVE, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_DONE_WORKER, complete_frame);
	nbdf_free(complete_frame);

	wo->mode = sim_worker_mode_complete;
	debugf("SWorker(%i): Simulation is complete.\n", wo->process_id);

	return 1;
}


void sim_worker_abortsim(sim_worker_tp wo, gchar * error) {
	nbdf_tp error_frame;

	error_frame = nbdf_construct("s", error);
	dvn_packet_route(DVNPACKET_MASTER, DVNPACKET_LAYER_SIM, 0, SIM_FRAME_ERROR, error_frame);
	nbdf_free(error_frame);

	/* set error mode flag */
	wo->mode = sim_worker_mode_error;

	debugf(LOG_ERR, "SWorker (%d): Aborting simulation. Error: %s\n", wo->process_id, error);
}

void sim_worker_destroy_node(sim_worker_tp wo, context_provider_tp cp) {
	/* free his dtimer endpoint */
	dtimer_destroy_timers(global_sim_context.sim_worker->timer_mgr, cp);

	resolver_remove_byaddr(wo->resolver, cp->vsocket_mgr->addr);

	/* free his ip! this will also free the associated module memory
	 * AND the context provider, vbuffer, and vsocket. */
	vci_free_ip(global_sim_context.sim_worker->vci_mgr, cp->vsocket_mgr->addr);

	/* free his globals and such */
	module_destroy_instance(cp->modinst);

	/* final free */
	free(cp);
}

gint sim_worker_opexec(sim_worker_tp wo, simop_tp op) {
	gint rv = 1;
	switch(op->type){
		case OP_LOAD_PLUGIN: {
			rv = sim_worker_opexec_load_plugin(wo, op);
			break;
		}
		case OP_LOAD_CDF: {
			rv = sim_worker_opexec_load_cdf(wo, op);
			break;
		}
		case OP_GENERATE_CDF: {
			rv = sim_worker_opexec_generate_cdf(wo, op);
			break;
		}
		case OP_CREATE_NETWORK: {
			rv = sim_worker_opexec_create_network(wo, op);
			break;
		}
		case OP_CONNECT_NETWORKS: {
			rv = sim_worker_opexec_connect_network(wo, op);
			break;
		}
		case OP_CREATE_HOSTNAME: {
			rv = sim_worker_opexec_create_hostname(wo, op);
			break;
		}
		case OP_CREATE_NODES: {
			rv = sim_worker_opexec_create_nodes(wo, op);
			break;
		}
		case OP_END: {
			rv = sim_worker_opexec_end(wo, op);
			break;
		}
		default: {
			dlogf(LOG_ERR, "sim_master_opexec: Unknown dsim operation!? ");
			break;
		}
	}

	if(rv == 1) {
		simop_destroy(op);
	}

	return rv;
}
