/*
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
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
#include <glib/gprintf.h>

#include <event2/event.h>
#include <event2/event_struct.h>

#include "shd-library.h"

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
#include "or.h"
#include "util.h"
#include "address.h"
#include "compat_libevent.h"
#include "compat.h"
#include "container.h"
#include "ht.h"
#include "memarea.h"
#include "mempool.h"
#include "torlog.h"
#include "tortls.h"
#include "buffers.h"
#include "config.h"
#include "cpuworker.h"
#include "dirserv.h"
#include "dirvote.h"
#include "hibernate.h"
#include "rephist.h"
#include "router.h"
#include "routerparse.h"
#include "onion.h"
#include "control.h"
#include "networkstatus.h"
//#include "src/common/OpenBSD_malloc_Linux.h"
#include "dns.h"
#include "circuitlist.h"
#include "policies.h"
#include "geoip.h"
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
	VTOR_DIRAUTH, VTOR_RELAY, VTOR_EXITRELAY, VTOR_CLIENT, VTOR_TORRENT, VTOR_BROWSER, VTOR_PING
};

/* run every 5 mins */
#define VTORFLOW_SCHED_PERIOD 60000

enum cpuwstate {
	CPUW_NONE,
	CPUW_READTYPE, CPUW_READTAG, CPUW_READCHALLENGE, CPUW_PROCESS, CPUW_WRITERESPONSE,
	CPUW_V2_READ, CPUW_V2_PROCESS, CPUW_V2_WRITE,
};

/** The tag specifies which circuit this onionskin was from. */
#define TAG_LEN 10

#ifdef SCALLION_USEV2CPUWORKER
/** Magic numbers to make sure our cpuworker_requests don't grow any
 * mis-framing bugs. */
#define CPUWORKER_REQUEST_MAGIC 0xda4afeed
#define CPUWORKER_REPLY_MAGIC 0x5eedf00d

/** A request sent to a cpuworker. */
typedef struct cpuworker_request_t {
  /** Magic number; must be CPUWORKER_REQUEST_MAGIC. */
  uint32_t magic;
  /** Opaque tag to identify the job */
  uint8_t tag[TAG_LEN];
  /** Task code. Must be one of CPUWORKER_TASK_* */
  uint8_t task;

  /** A create cell for the cpuworker to process. */
  create_cell_t create_cell;

  /* Turn the above into a tagged union if needed. */
} cpuworker_request_t;

/** A reply sent by a cpuworker. */
typedef struct cpuworker_reply_t {
  /** Magic number; must be CPUWORKER_REPLY_MAGIC. */
  uint32_t magic;
  /** Opaque tag to identify the job; matches the request's tag.*/
  uint8_t tag[TAG_LEN];
  /** True iff we got a successful request. */
  uint8_t success;

  /** Output of processing a create cell
   *
   * @{
   */
  /** The created cell to send back. */
  created_cell_t created_cell;
  /** The keys to use on this circuit. */
  uint8_t keys[CPATH_KEY_MATERIAL_LEN];
  /** Input to use for authenticating introduce1 cells. */
  uint8_t rend_auth_material[DIGEST_LEN];
} cpuworker_reply_t;

typedef struct vtor_cpuworker_s {
	int fd;
	server_onion_keys_t onion_keys;
	cpuworker_request_t req;
	cpuworker_reply_t rpl;
	struct event read_event;
	uint offset;
	enum cpuwstate state;
} vtor_cpuworker_t, *vtor_cpuworker_tp;
#else
/** How many bytes are sent from the cpuworker back to tor? */
#define LEN_ONION_RESPONSE (1+TAG_LEN+ONIONSKIN_REPLY_LEN+CPATH_KEY_MATERIAL_LEN)
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
#endif

typedef struct vtor_logfile_s {
    struct logfile_t *next; /**< Next logfile_t in the linked list. */
    char *filename; /**< Filename to open. */
    int fd; /**< fd to receive log messages, or -1 for none. */
    int seems_dead; /**< Boolean: true if the stream seems to be kaput. */
    int needs_close; /**< Boolean: true if the stream gets closed on shutdown. */
    int is_temporary; /**< Boolean: close after initializing logging subsystem.*/
    int is_syslog; /**< Boolean: send messages to syslog. */
    log_callback callback; /**< If not NULL, send messages to this function. */
    log_severity_list_t *severities; /**< Which severity of messages should we
                                      * log for each log domain? */
} vtor_logfile_t, *vtor_logfile_tp;

typedef struct _ScallionTor ScallionTor;
struct _ScallionTor {
	char v3bw_name[255];
	enum vtor_nodetype type;
	unsigned int bandwidth;
	int refillmsecs;
	vtor_cpuworker_tp cpuw;
	GList *logfiles;
	ShadowFunctionTable* shadowlibFuncs;
};

typedef struct _Scallion Scallion;
struct _Scallion {
	in_addr_t ip;
	gchar ipstring[40];
	gchar hostname[128];
	ScallionTor* stor;
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
