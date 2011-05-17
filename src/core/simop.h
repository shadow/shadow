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

#ifndef _simop_h
#define _simop_h

#include <netinet/in.h>

#include "evtracker.h"
#include "nbdf.h"
#include "dsim_utils.h"

#define SIMOP_CODE_CNODES        1
#define SIMOP_CODE_NETWORK       2
#define SIMOP_CODE_END           3
#define SIMOP_CODE_MODLOAD       4

#define SIMOP_CNODES_BOOTSTRAP   1
#define SIMOP_CNODES_TRACKING    2

#define SIMOP_NETWORK_CREATE     1
#define SIMOP_NETWORK_DISCONNECT 2
#define SIMOP_NETWORK_CONNECT    3

#define SIMOP_STRING_LEN 256
#define SIMOP_CNODES_CLARGS_LEN 512

/* TODO perfect place for inheritance */
typedef struct simop_t {
	ptime_t target_time;
	enum operation_type type;
	/* type simop_*, depending on type */
	void* operation;

//	union {
//		struct {
//			unsigned int quantity;
//
//			unsigned int network_id;
//			unsigned int module_id;
//
//			/* hostname to use for this node. if quantity > 1, a unique id is appended */
//			char hostname[SIMOP_STRING_LEN];
//
//			unsigned int logging_level;
//			unsigned char logging_channel;
//
//			/* kilobytes per second */
//			unsigned int KBps_down;
//			unsigned int KBps_up;
//
//			char flags;
//
//			unsigned int tracker_id;
//			unsigned int bootstrap_id;
//
//			char cl_args[SIMOP_CNODES_CLARGS_LEN];
//		} load_plugin;
//
//		struct {
//			unsigned int id;
//			char file[200];
//			unsigned int argc;
//			char ** argv;
//		} modload;
//
//		struct {
//			 unsigned int id1, id2;
//			 unsigned int base_delay, width, tail_width, reliability;
//			 unsigned char method;
//		} network;
//	} detail;
} simop_t, * simop_tp;

typedef struct simop_load_plugin_s {
	unsigned int id;
	char filepath[SIMOP_STRING_LEN];
} simop_load_plugin_t, *simop_load_plugin_tp;

typedef struct simop_load_cdf_s {
	unsigned int id;
	char filepath[SIMOP_STRING_LEN];
} simop_load_cdf_t, *simop_load_cdf_tp;

typedef struct simop_generate_cdf_s {
	unsigned int id, base_delay, base_width, tail_width;
} simop_generate_cdf_t, *simop_generate_cdf_tp;

typedef struct simop_create_network_s {
	unsigned int id, cdf_id_intra_latency;
	double reliability;
} simop_create_network_t, *simop_create_network_tp;

typedef struct simop_connect_networks_s {
	unsigned int network1_id, cdf_id_latency_1to2, network2_id, cdf_id_latency_2to1;
	double reliability_1to2, reliability_2to1;
} simop_connect_networks_t, *simop_connect_networks_tp;

typedef struct simop_create_hostname_s {
	unsigned int id;
	char base_hostname[SIMOP_STRING_LEN];
} simop_create_hostname_t, *simop_create_hostname_tp;

typedef struct simop_create_nodes_s {
	unsigned int quantity, plugin_id, network_id, hostname_id,
	cdf_id_bandwidth_up, cdf_id_bandwidth_down, cdf_id_cpu_speed, hostname_unique_counter;
	char cl_args[SIMOP_CNODES_CLARGS_LEN];
} simop_create_nodes_t, *simop_create_nodes_tp;

typedef struct simop_end_s {
	ptime_t end_time;
} simop_end_t, *simop_end_tp;

typedef evtracker_tp simop_list_tp;
#define simop_earliest_event evtracker_earliest_event

simop_list_tp simop_list_create (void) ;
void simop_list_destroy( simop_list_tp list );
void simop_list_add( simop_list_tp list, simop_tp op, ptime_t time);
int simop_list_size( simop_list_tp list );
simop_tp simop_get_next( simop_list_tp list, ptime_t * time );
simop_tp simop_look_next( simop_list_tp list, ptime_t * time);

/* network encoding/decoding */
nbdf_tp simop_nbdf_encode(operation_tp dsimop, unsigned int tracking_id);
simop_tp simop_nbdf_decode(nbdf_tp nb);

void simop_destroy(simop_tp simop);

#endif

