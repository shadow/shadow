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
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <glib.h>
#include <gmodule.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include "shd-library.h"
#include "shd-filetransfer.h"
#include "shd-browser.h"
#include "shd-torrent-service.h"

/* includes from Tor */
#undef NDEBUG
//#ifndef _GNU_SOURCE
//#define _GNU_SOURCE 1
//#endif
# ifndef __daddr_t_defined
typedef __daddr_t daddr_t;
typedef __caddr_t caddr_t;
#  define __daddr_t_defined
# ifndef __u_char_defined
# endif
typedef __u_char u_char;
typedef __u_short u_short;
typedef __u_int u_int;
typedef __u_long u_long;
typedef __quad_t quad_t;
typedef __u_quad_t u_quad_t;
typedef __fsid_t fsid_t;
#  define __u_char_defined
# endif

#include "orconfig.h"
#include "src/or/or.h"
#include "src/common/util.h"
#include "src/common/address.h"
#include "src/common/compat_libevent.h"
#include "src/common/compat.h"
#include "src/common/container.h"
#include "src/common/ht.h"
#include "src/common/memarea.h"
#include "src/common/mempool.h"
#include "src/common/torlog.h"
#include "src/common/tortls.h"
#include "src/or/buffers.h"
#include "src/or/config.h"
#include "src/or/cpuworker.h"
#include "src/or/dirserv.h"
#include "src/or/dirvote.h"
#include "src/or/hibernate.h"
#include "src/or/rephist.h"
#include "src/or/router.h"
#include "src/or/routerparse.h"
#include "src/or/onion.h"
#include "src/or/control.h"
#include "src/or/networkstatus.h"
//#include "src/common/OpenBSD_malloc_Linux.h"
#include "src/or/dns.h"
#include "src/or/circuitlist.h"
#include "src/or/policies.h"
#include "src/or/geoip.h"
#include <openssl/bn.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <time.h>

/* externals from Tor */
extern void socket_accounting_lock();
extern void socket_accounting_unlock();
extern routerlist_t *router_get_routerlist(void);
extern struct event_base * tor_libevent_get_base(void);
extern void tor_cleanup(void);
extern void second_elapsed_callback(periodic_timer_t *timer, void *arg);
extern void refill_callback(periodic_timer_t *timer, void *arg);
extern int init_keys(void);
extern void init_cell_pool(void);
extern void connection_bucket_init(void);
extern int trusted_dirs_reload_certs(void);
extern int router_reload_router_list(void);
extern void directory_info_has_arrived(time_t now, int from_cache);
extern int tor_init(int argc, char *argv[]);

extern int n_sockets_open;
extern int global_write_bucket;
extern int stats_prev_global_write_bucket;
extern int global_read_bucket;
extern int stats_prev_global_read_bucket;
extern periodic_timer_t * second_timer;
extern periodic_timer_t * refill_timer;
extern smartlist_t * active_linked_connection_lst;
extern crypto_pk_t * client_identitykey;
extern int called_loop_once;

enum vtor_nodetype {
	VTOR_DIRAUTH, VTOR_RELAY, VTOR_EXITRELAY, VTOR_CLIENT, VTOR_TORRENT, VTOR_BROWSER,
};

/** The tag specifies which circuit this onionskin was from. */
#define TAG_LEN 10
/** How many bytes are sent from the cpuworker back to tor? */
#define LEN_ONION_RESPONSE (1+TAG_LEN+ONIONSKIN_REPLY_LEN+CPATH_KEY_MATERIAL_LEN)

/* run every 5 mins */
#define VTORFLOW_SCHED_PERIOD 60000

enum cpuwstate {
	CPUW_READTYPE, CPUW_READTAG, CPUW_READCHALLENGE, CPUW_PROCESS, CPUW_WRITERESPONSE
};

typedef struct vtor_cpuworker_s {
	int fd;
	char question[ONIONSKIN_CHALLENGE_LEN];
	uint8_t question_type;
	char keys[CPATH_KEY_MATERIAL_LEN];
	char reply_to_proxy[ONIONSKIN_REPLY_LEN];
	char buf[LEN_ONION_RESPONSE];
	char tag[TAG_LEN];
	crypto_pk_t *onion_key;
	crypto_pk_t *last_onion_key;
	struct event read_event;
	uint offset;
	enum cpuwstate state;
} vtor_cpuworker_t, *vtor_cpuworker_tp;

typedef struct _ScallionTor ScallionTor;
struct _ScallionTor {
	char v3bw_name[255];
	enum vtor_nodetype type;
	unsigned int bandwidth;
	int refillmsecs;
	vtor_cpuworker_tp cpuw;
	ShadowFunctionTable* shadowlibFuncs;
};

typedef struct _Scallion Scallion;
struct _Scallion {
	in_addr_t ip;
	gchar ipstring[40];
	gchar hostname[128];
	ScallionTor* stor;
	service_filegetter_t sfg;
	gint sfgEpoll;
	TorrentService tsvc;
	gint tsvcClientEpoll;
	gint tsvcServerEpoll;
	browser_t browser;
	gint browserEpoll;
	ShadowFunctionTable* shadowlibFuncs;
};

extern Scallion scallion;
#undef log

void scallionpreload_init(GModule* handle);

ScallionTor* scalliontor_new(ShadowFunctionTable* shadowlibFuncs,
		char* hostname, enum vtor_nodetype type, char* bandwidth,
		char* bwrate, char* bwburst,
		char* torrc_path, char* datadir_path, char* geoip_path);
void scalliontor_notify(ScallionTor* stor);
void scalliontor_free(ScallionTor* stor);

#endif /* SCALLION_H_ */
