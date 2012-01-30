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

#ifndef SCALLION_H_
#define SCALLION_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>
#include <shd-library.h>
#include <shd-filetransfer.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include "torlog.h"

#include "tor_includes.h"
#include "tor_externs.h"

/* externals from Tor */
extern int n_sockets_open;
extern void socket_accounting_lock();
extern void socket_accounting_unlock();
extern routerlist_t *router_get_routerlist(void);
extern struct event_base * tor_libevent_get_base(void);
extern void tor_cleanup(void);
extern void second_elapsed_callback(periodic_timer_t *timer, void *arg);
extern void refill_callback(periodic_timer_t *timer, void *arg);
extern int identity_key_is_set(void);
extern int init_keys(void);
extern void init_cell_pool(void);
extern void connection_bucket_init(void);
extern int trusted_dirs_reload_certs(void);
extern int router_reload_router_list(void);
extern void directory_info_has_arrived(time_t now, int from_cache);
extern int tor_init(int argc, char *argv[]);

enum vtor_nodetype {
	VTOR_DIRAUTH, VTOR_RELAY, VTOR_EXITRELAY, VTOR_CLIENT
};

/** The tag specifies which circuit this onionskin was from. */
#define TAG_LEN 10
/** How many bytes are sent from the cpuworker back to tor? */
#define LEN_ONION_RESPONSE (1+TAG_LEN+ONIONSKIN_REPLY_LEN+CPATH_KEY_MATERIAL_LEN)

/* run every 5 mins */
#define VTORFLOW_SCHED_PERIOD 60000

typedef struct vtor_cpuworker_s {
	int fd;
	char question[ONIONSKIN_CHALLENGE_LEN];
	uint8_t question_type;
	char keys[CPATH_KEY_MATERIAL_LEN];
	char reply_to_proxy[ONIONSKIN_REPLY_LEN];
	char buf[LEN_ONION_RESPONSE];
	char tag[TAG_LEN];
	crypto_pk_env_t *onion_key;
	crypto_pk_env_t *last_onion_key;
	struct event read_event;
} vtor_cpuworker_t, *vtor_cpuworker_tp;

typedef struct _ScallionTor ScallionTor;
struct _ScallionTor {
	char v3bw_name[255];
	enum vtor_nodetype type;
	unsigned int bandwidth;
	int refillmsecs;
	vtor_cpuworker_tp cpuw;
	ShadowlibFunctionTable* shadowlibFuncs;
};

typedef struct _Scallion Scallion;
struct _Scallion {
	in_addr_t ip;
	gchar ipstring[40];
	gchar hostname[128];
	ScallionTor* stor;
	service_filegetter_t sfg;
	gint sfgEpoll;
	ShadowlibFunctionTable* shadowlibFuncs;
};

extern Scallion scallion;
#undef log

void scallion_register_globals(PluginFunctionTable* scallionFuncs, Scallion* scallionData);

ScallionTor* scalliontor_new(ShadowlibFunctionTable* shadowlibFuncs,
		char* hostname, enum vtor_nodetype type, char* bandwidth,
		char* bwrate, char* bwburst,
		char* torrc_path, char* datadir_path, char* geoip_path);
void scalliontor_notify(ScallionTor* stor);
void scalliontor_free(ScallionTor* stor);

#endif /* SCALLION_H_ */
