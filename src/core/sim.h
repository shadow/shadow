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

#ifndef _sim_h
#define _sim_h

#include <netinet/in.h>

#include "global.h"
#include "dsim_utils.h"
#include "simop.h"
#include "module.h"
#include "timer.h"
#include "vci.h"
#include "hashtable.h"
#include "simnet_graph.h"
#include "resolver.h"

#define TICKTOCK_INTERVAL 1000

typedef struct ticktock_event_s {
	ptime_t time_scheduled;
} ticktock_event_t, *ticktock_event_tp;

typedef struct sim_worker_remotestate_t {
	/* time of last event processed by worker */
	ptime_t last_event;
	/* time of next event in our queue */
	ptime_t next_event;
	/*  time window barrier for simulating - worker allowed to run events until this time is reached */
	ptime_t window;
	/* the current barrier we are waiting at.
	 * if our next event is beyond our window, current is the window.
	 * o/w current is the next event. (i.e. our current notion of the "current" safe time)
	 */
	ptime_t current;
	unsigned char valid;
} sim_worker_remotestate_t, * sim_worker_remotestate_tp;

typedef struct sim_worker_t {
	enum { sim_worker_mode_idle, sim_worker_mode_spool, sim_worker_mode_simulating, sim_worker_mode_error, sim_worker_mode_complete } mode;

	/** this worker's process id. should never be 0, since that's the main process. */
	unsigned int process_id;

	dtimer_mgr_tp timer_mgr;
	vci_mgr_tp vci_mgr;
	module_mgr_tp mod_mgr;

	hashtable_tp hostname_tracking;
	hashtable_tp loaded_cdfs;

	events_tp events;
	list_tp stalled_simops;
	vci_addressing_scheme_tp ascheme;

	/* internal hostname to address resolver */
	resolver_tp resolver;

	simnet_graph_tp network_topology;

	/* for runahead computation, see explanation in sim_master_t below */
	ptime_t min_latency; /**< Smallest delay in event delivery */
	ptime_t max_latency; /**< Largest delay in event delivery */

	/** current time of simulation. */
	ptime_t current_time;
	ptime_t last_broadcast;
	struct timespec wall_time_at_startup;

	/** the state of each other worker (plus 0 state for externals hosts, from process 0) */
	sim_worker_remotestate_tp worker_states;
	sim_worker_remotestate_tp my_state;

	/** total number of workers on this machine */
	unsigned int num_workers;

	pipecloud_tp pipecloud;
	int destroying;
} sim_worker_t, * sim_worker_tp;


typedef struct sim_slave_t {
	unsigned int my_id;
	unsigned int num_workers;
	unsigned int num_workers_complete;
	unsigned int worker_turn;
} sim_slave_t, * sim_slave_tp;


typedef struct sim_worker_nodetracker_t {
	char valid;
	in_addr_t addr;
	int track_id;
} sim_worker_nodetracker_t, * sim_worker_nodetracker_tp;

typedef struct sim_master_tracker_t {
	unsigned int id;
	unsigned int counter;
	void* value;
} sim_master_tracker_t, *sim_master_tracker_tp;

/* the simulation master; one exists per simulation */
typedef struct sim_master_t {
	dsim_tp dsim;

	size_t num_slaves; /**< Number of slaves */
	size_t num_slaves_complete; /**< Number of slaves that are finished simulating */

	/* contains network topology and latencies, populated from dsim */
	simnet_graph_tp network_topology;

	/* these are basically used to ensure unique ids */
	hashtable_tp module_tracking; /**< Tracking HT for sim_modtracker_t objects */
	hashtable_tp network_tracking; /**< Tracking HT for sim_nettracker_t objects */
	hashtable_tp cdf_tracking;
	hashtable_tp base_hostname_tracking;

	struct timespec simulation_start; /**< Real time start of simulation (for timing) */

	int end_time_found;
} sim_master_t, * sim_master_tp;


sim_master_tp sim_master_create (char * dsim, unsigned int num_slaves);
void sim_master_destroy(sim_master_tp sim);
void sim_master_deposit(sim_master_tp sslave, int frametype, nbdf_tp nb);

void sim_master_opexec(sim_master_tp ma, operation_tp op) ;
int sim_master_isdone(sim_master_tp);

void sim_slave_deposit(sim_slave_tp sslave, int frametype, nbdf_tp frame);
sim_slave_tp sim_slave_create (unsigned int my_id, unsigned int num_workers);
void sim_slave_destroy(sim_slave_tp sslave);


sim_worker_tp sim_worker_create (pipecloud_tp pipecloud,
		int slave_id, int process_id, unsigned int num_slaves, unsigned int num_workers, unsigned int max_wrkrs_per_slave);
void sim_worker_deposit(sim_worker_tp worker, int frametype, nbdf_tp frame);

//void sim_worker_timecalc(sim_worker_tp worker);
//int sim_worker_setstate(sim_worker_tp worker, ptime_t last_event_time, ptime_t next_event_time);

int sim_worker_heartbeat(sim_worker_tp worker, size_t* num_event_worker_executed);
int sim_worker_opexec(sim_worker_tp wo, simop_tp sop) ;
void sim_worker_abortsim(sim_worker_tp wo, char * error) ;
void sim_worker_destroy_node(sim_worker_tp wo, context_provider_tp cp);
void sim_worker_destroy(sim_worker_tp sim);
sim_worker_nodetracker_tp sim_worker_create_nodetracker(in_addr_t addr, int track_id, char valid);
void sim_worker_destroy_nodetracker_cb(void* value, int key);
void sim_worker_destroy_nodetracker(sim_worker_nodetracker_tp nt);


//#define SIM_WOSTATE_IDLE 1 /* ? */
//#define SIM_WOSTATE_WORKING 2 /* working on the current time cycle */
//#define SIM_WOSTATE_STALLED 4
//#define SIM_WOSTATE_ENDING 5
//#define SIM_WOSTATE_ERROR 6
//
//
//typedef struct sim_worker_wostate_t {
//	ptime_t completed;
//	ptime_t next_event;
//	ptime_t last_processed;
//} sim_worker_wostate_t, * sim_worker_wostate_tp;
//
///* any worker, local or otherwise */
//typedef struct sim_worker_t {
//	simop_list_tp oplist;
//
//	/* vci and timer manager here */
//	dtimer_mgr_tp timer_mgr;
//	vci_mgr_tp vci_mgr;
//
//	module_mgr_tp mod_mgr;
//
//	hashtable_tp node_tracking;
//
//	char state;
//
//	int my_id;
//
//	/** the current simulation time */
//	ptime_t curtime;
//	ptime_t reported_complete;
//
//	/** Current time allownace. We can process up to and including this time */
//	ptime_t allowance;
//
//	/** The allowed lag value. Constant, precomputed. */
//	ptime_t min_net_delay;
//
//	/** The jump window size. Maximum time after an eve3nt which a packet may arrive. */
//	ptime_t max_net_delay;
//
//	/** total number of workers */
//	size_t num_workers;
//
//	/** Known state of each worker */
//	sim_worker_wostate_tp worker_states;
//
//	/** packet spool for incoming data */
//	nbdf_tp packet_spool;
//
//	/** thread shiz */
//	pthread_t * threads;
//	size_t num_threads;
//	int thread_end;
//
//	pthread_cond_t thread_active_cond;
//	pthread_mutex_t thread_active_mutex;
//
//	pthread_cond_t thread_work_cond;
//	pthread_mutex_t thread_work_mutex;
//
//	ptime_t thread_work_stoptime;
//	size_t num_active_threads;
//} sim_worker_t, * sim_worker_tp;
//
//typedef struct nodeholder_t {
//	void *handle;
//	in_addr_t addr;
//} nodeholder_t, *nodeholder_tp;
//
//
//typedef struct sim_nettracker_t {
//	int id;
//} sim_nettracker_t, *sim_nettracker_tp;
//
//typedef struct sim_modtracker_t {
//	int id;
//} sim_modtracker_t, *sim_modtracker_tp;
//
//typedef struct sim_nodetracker_t {
//	char valid;
//	unsigned int track_id;
//	in_addr_t addr;
//
//	simop_tp ip_simop;
//} sim_nodetracker_t, *sim_nodetracker_tp;
//
//#define SIM_MASTATE_NONE  0
//#define SIM_MASTATE_WPROC 2 /**< Workers are processing. wait... */
//#define SIM_MASTATE_ERR	  3 /**< erroneous state */
//#define SIM_MASTATE_ENDING 4 /**< master is waiting for all workers to report complete.... */
//#define SIM_MASTATE_DONE  5
//#define SIM_MASTATE_SPOOLING 6
/*
#define SIM_WFRAME_CMD_PROCTIME 1
#define SIM_WFRAME_CMD_OP 2
#define SIM_WFRAME_CMD_VCIDELIV 3
#define SIM_WFRAME_CMD_TIMEALLOW 4
#define SIM_WFRAME_CMD_TRACKRESP 6
#define SIM_WFRAME_CMD_ENDERROR 7
#define SIM_WFRAME_CMD_STARTSIM 8
#define SIM_WFRAME_CMD_SIMSTATE 9

#define SIM_MFRAME_CMD_DONETIME 1
#define SIM_MFRAME_CMD_TRACKRESP 2
#define SIM_MFRAME_CMD_ENDERROR 3
#define SIM_MFRAME_CMD_COMPLETE 4
*/

/**
  * Creates a sim_master_tp
  * @return The newly created master object
  */
//sim_master_tp sim_master_create (char * dsim, size_t num_workers);
//
///* sends the given frame to the master */
//void sim_master_deposit (sim_master_tp ma, int source_worker_id, nbdf_tp nb);
//
///* tells the master to process some quanta of work */
//int sim_master_process (sim_master_tp ma);
//
//void sim_master_opexec(sim_master_tp ma, operation_tp op);
//void sim_master_opexec_load_module(sim_master_tp ma, operation_tp op);
//void sim_master_opexec_create_network(sim_master_tp ma, operation_tp op);
//void sim_master_opexec_connect_network(sim_master_tp ma, operation_tp op);
//void sim_master_opexec_disconnect_network(sim_master_tp ma, operation_tp op);
//void sim_master_opexec_end_sim(sim_master_tp ma, operation_tp op) ;
//void sim_master_opexec_create_nodes(sim_master_tp ma, operation_tp op);
//
///* returns nonzero if the master is completed working */
//int sim_master_isdone (sim_master_tp ma);
//
///* destroy the master */
//void sim_master_destroy(sim_master_tp sim);
//
//
//
///* create a worker */
//sim_worker_tp sim_worker_create (int my_id, size_t num_workers);
//
///* sends the given frame to a worker */
//void sim_worker_deposit (sim_worker_tp wo, int source_worker_id, nbdf_tp);
//
///* tells a worker to process some quanta of work */
//int sim_worker_process (sim_worker_tp wo);
//
//void sim_worker_opexec_cnodes(sim_worker_tp wo, simop_tp sop);
//void sim_worker_opexec_end(sim_worker_tp wo, simop_tp sop);
//void sim_worker_opexec_module_load(sim_worker_tp wo, simop_tp sop);
//void sim_worker_opexec_network(sim_worker_tp wo, simop_tp sop);
//void sim_worker_opexec(sim_worker_tp wo, simop_tp sop);
//
///* destroy the worker */
//void sim_worker_destroy(sim_worker_tp sim);
//void sim_worker_abortsim(sim_worker_tp wo, char * error);
//
//void sim_tell_worker(int worker, int command, nbdf_tp nb);
//void sim_tell_master(int command, nbdf_tp nb);
//void sim_master_wbroadcast(sim_master_tp ma, int command, nbdf_tp nb);
//void sim_worker_wbroadcast(sim_worker_tp wo, int command, nbdf_tp nb);
//
//
//void sim_hashwalk_nodetracker(void * d, int id) ;
//
//void sim_hashwalk_nettracker(void * d, int id);
//
//void sim_hashwalk_modtracker(void * d, int id) ;


#endif

