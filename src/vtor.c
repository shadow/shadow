/*
 * dvn_tor.c
 *
 *  Created on: Apr 8, 2010
 *      Author: jansen
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <shd-plugin.h>
#include "scallion.h"
#include "vtor.h"
#include "vtorflow.h"

#include <event2/event.h>
#include <event2/event_struct.h>

#include "tor_includes.h"
#include "tor_externs.h"

/** The tag specifies which circuit this onionskin was from. */
#define TAG_LEN 10
/** How many bytes are sent from the cpuworker back to tor? */
#define LEN_ONION_RESPONSE \
  (1+TAG_LEN+ONIONSKIN_REPLY_LEN+CPATH_KEY_MATERIAL_LEN)

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

/* Forward declarations. */
static int vtor_run(int argc, char *argv[]);
static int vtor_do_main_loop(void);

/* external functions from Tor */
extern struct event_base * tor_libevent_get_base(void);
extern void tor_cleanup(void);
extern void second_elapsed_callback(periodic_timer_t *timer, void *arg);
extern int identity_key_is_set(void);
extern int init_keys(void);
extern void init_cell_pool(void);
extern void connection_bucket_init(void);
extern int trusted_dirs_reload_certs(void);
extern int router_reload_router_list(void);
extern void directory_info_has_arrived(time_t now, int from_cache);
extern int tor_init(int argc, char *argv[]);

void vtor_instantiate(vtor_tp vtor, char* hostname, enum vtor_nodetype type,
		char* bandwidth, char* torrc_path, char* datadir_path, char* geoip_path) {
	if(vtor != NULL) {
		vtor->type = type;
		vtor->bandwidth = atoi(bandwidth);

		/* we use 14 args to tor by 'default' */
		int num_args = 19;
		if(vtor->type == VTOR_DIRAUTH || vtor->type == VTOR_RELAY) {
			num_args += 2;
		}

		char bwconf[128];
		snprintf(bwconf, 128, "%s KB", bandwidth);

		int cap = vtor->bandwidth + 5120;
		int burst = vtor->bandwidth * 2;
		if(burst > cap) {
			burst = cap;
		}
		char burstconf[128];
		snprintf(burstconf, 128, "%i KB", burst);

		/* default args */
		char *config[num_args];
		config[0] = "tor";
		config[1] = "--Address";
		config[2] = hostname;
		config[3] = "-f";
		config[4] = torrc_path;
		config[5] = "--DataDirectory";
		config[6] = datadir_path;
		config[7] = "--GeoIPFile";
		config[8] = geoip_path;
		config[9] = "--BandwidthRate";
		config[10] = bwconf;
		config[11] = "--BandwidthBurst";
		config[12] = bwconf;
		config[13] = "--MaxAdvertisedBandwidth";
		config[14] = bwconf;
		config[15] = "--RelayBandwidthRate";
		config[16] = bwconf;
		config[17] = "--RelayBandwidthBurst";
		config[18] = bwconf;

		/* additional args */
		if(vtor->type == VTOR_DIRAUTH) {
			if(snprintf(vtor->v3bw_name, 255, "%s/dirauth.v3bw", datadir_path) >= 255) {
				/* truncated is an error here */
				snri_log(LOG_WARN, "vtor_instantiate: v3bw name too long! failing.\n");
				return;
			}
			config[19] = "--V3BandwidthsFile";
			config[20] = vtor->v3bw_name;
		} else if(vtor->type == VTOR_RELAY) {
			config[19] = "--ExitPolicy";
			config[20] = "reject *:*";
		}

		snri_log(LOG_MSG, "vtor_instantiate: booting the Tor node\n");

		vtor_run(num_args, config);

		if(vtor->type == VTOR_DIRAUTH) {
			/* run torflow now, it will schedule itself as needed */
			vtorflow_init_v3bw(vtor->v3bw_name);
		}

		snri_log(LOG_MSG, "vtor_instantiate: Tor node is running!\n");
	}
}

void vtor_destroy() {
	tor_cleanup();
	snri_log(LOG_MSG, "vtor_destroy: Tor node destroyed\n");
}

static int vtor_run(int argc, char *argv[])
{
	int result = 0;
	update_approx_time(time(NULL));
	tor_threads_init();
	init_logging();

	if (tor_init(argc, argv)<0)
		return -1;
	result = vtor_do_main_loop();

	return result;
}

static int vtor_do_main_loop(void)
{
//  int loop_result;
  time_t now;

  /* initialize dns resolve map, spawn workers if needed */
//  if (dns_init() < 0) {
//    if (get_options()->ServerDNSAllowBrokenConfig)
//      log_warn(LD_GENERAL, "Couldn't set up any working nameservers. "
//               "Network not up yet?  Will try again soon.");
//    else {
//      log_err(LD_GENERAL,"Error initializing dns subsystem; exiting.  To "
//              "retry instead, set the ServerDNSAllowBrokenResolvConf option.");
//    }
//  }

//  handle_signals(1);

  /* load the private keys, if we're supposed to have them, and set up the
   * TLS context. */
  if (! identity_key_is_set()) {
    if (init_keys() < 0) {
      log_err(LD_BUG,"Error initializing keys; exiting");
      return -1;
    }
  }

  /* Set up the packed_cell_t memory pool. */
  init_cell_pool();

  /* Set up our buckets */
  connection_bucket_init();
  stats_prev_global_read_bucket = global_read_bucket;
  stats_prev_global_write_bucket = global_write_bucket;

  /* initialize the bootstrap status events to know we're starting up */
  control_event_bootstrap(BOOTSTRAP_STATUS_STARTING, 0);

  if (trusted_dirs_reload_certs()) {
    log_warn(LD_DIR,
             "Couldn't load all cached v3 certificates. Starting anyway.");
  }
  if (router_reload_v2_networkstatus()) {
    return -1;
  }
  if (router_reload_consensus_networkstatus()) {
    return -1;
  }
  /* load the routers file, or assign the defaults. */
  if (router_reload_router_list()) {
    return -1;
  }
  /* load the networkstatuses. (This launches a download for new routers as
   * appropriate.)
   */
  now = time(NULL);
  directory_info_has_arrived(now, 1);

  /* !note that scallion intercepts the cpuworker functionality (rob) */
  if (server_mode(get_options())) {
    /* launch cpuworkers. Need to do this *after* we've read the onion key. */
    cpu_init();
  }

  /* set up once-a-second callback. */
  if (! second_timer) {
    struct timeval one_second;
    one_second.tv_sec = 1;
    one_second.tv_usec = 0;

    second_timer = periodic_timer_new(tor_libevent_get_base(),
                                      &one_second,
                                      second_elapsed_callback,
                                      NULL);
    tor_assert(second_timer);
  }

//  for (;;) {
//    if (nt_service_is_stopping())
//      return 0;
//
//#ifndef MS_WINDOWS
//     Make it easier to tell whether libevent failure is our fault or not.
//    errno = 0;
//#endif
//     All active linked conns should get their read events activated.
//    SMARTLIST_FOREACH(active_linked_connection_lst, connection_t *, conn,
//                      event_active(conn->read_event, EV_READ, 1));
//    called_loop_once = smartlist_len(active_linked_connection_lst) ? 1 : 0;
//
//    update_approx_time(time(NULL));
//
//     poll until we have an event, or the second ends, or until we have
//     * some active linked connections to trigger events for.
//    loop_result = event_base_loop(tor_libevent_get_base(),
//                                  called_loop_once ? EVLOOP_ONCE : 0);
//
//     let catch() handle things like ^c, and otherwise don't worry about it
//    if (loop_result < 0) {
//      int e = tor_socket_errno(-1);
//       let the program survive things like ^z
//      if (e != EINTR && !ERRNO_IS_EINPROGRESS(e)) {
//        log_err(LD_NET,"libevent call with %s failed: %s [%d]",
//                tor_libevent_get_method(), tor_socket_strerror(e), e);
//        return -1;
//#ifndef MS_WINDOWS
//      } else if (e == EINVAL) {
//        log_warn(LD_NET, "EINVAL from libevent: should you upgrade libevent?");
//        if (got_libevent_error())
//          return -1;
//#endif
//      } else {
//        if (ERRNO_IS_EINPROGRESS(e))
//          log_warn(LD_BUG,
//                   "libevent call returned EINPROGRESS? Please report.");
//        log_debug(LD_NET,"libevent call interrupted.");
//         You can't trust the results of this poll(). Go back to the
//         * top of the big for loop.
//        continue;
//      }
//    }
//  }
  return 0;
}

void vtor_socket_readable(vtor_tp vtor, int sockd) {
	update_approx_time(time(NULL));
}

void vtor_socket_writable(vtor_tp vtor, int sockd) {
	update_approx_time(time(NULL));
}

/* tor sometimes call event_base_loopexit so it can activate "linked" socks conns */
void vtor_loopexit_cb(int unused1, void* unused2) {
	update_approx_time(time(NULL));

    /* All active linked conns should get their read events activated. */
    SMARTLIST_FOREACH(active_linked_connection_lst, connection_t *, conn,
    		event_active(conn->read_event, EV_READ, 1));

    called_loop_once = smartlist_len(active_linked_connection_lst) ? 1 : 0;

    /* check for remaining active connections */
    if(called_loop_once) {
    	/* call back so we can check the linked conns again */
    	snri_timer_create(10, &vtor_loopexit_cb, NULL);
    }
}

extern void socket_accounting_lock();
extern int n_sockets_open;
extern void socket_accounting_unlock();

int intercept_tor_open_socket(int domain, int type, int protocol)
{
  int s = socket(domain, type | SOCK_NONBLOCK, protocol);
  if (s >= 0) {
    socket_accounting_lock();
    ++n_sockets_open;
//    mark_socket_open(s);
    socket_accounting_unlock();
  }
  return s;
}

void intercept_tor_gettimeofday(struct timeval *timeval) {
	if(timeval != NULL) {
		snri_gettime(timeval);
	}
}

#include "torlog.h"

void intercept_logv(int severity, uint32_t domain, const char *funcname,
     const char *format, va_list ap) {
	char* sev_str = NULL;
	const size_t buflen = 10024;
	char buf[buflen];
	size_t current_position = 0;

	/* Call assert, not tor_assert, since tor_assert calls log on failure. */
	assert(format);

	switch (severity) {
		case LOG_DEBUG:
			sev_str = "tor-debug";
		break;

		case LOG_INFO:
			sev_str = "tor-info";
		break;

		case LOG_NOTICE:
			sev_str = "tor-notice";
		break;

		case LOG_WARN:
			sev_str = "tor-warn";
		break;

		case LOG_ERR:
			sev_str = "tor-err";
		break;

		default:
			sev_str = "tor-UNKNOWN";
		break;
	}

	snprintf(&buf[current_position], strlen(sev_str)+4, "[%s] ", sev_str);
	current_position += strlen(sev_str)+3;

	if (domain == LD_BUG) {
		snprintf(&buf[current_position], 6, "BUG: ");
		current_position += 5;
	}

	if(funcname != NULL) {
		snprintf(&buf[current_position], strlen(funcname)+4, "%s() ", funcname);
		current_position += strlen(funcname)+3;
	}

	size_t size = buflen-current_position-2;
	int res = vsnprintf(&buf[current_position], size, format, ap);

	if(res >= size) {
		/* truncated */
		current_position = buflen - 3;
	} else {
		current_position += res;
	}

	buf[current_position] = '\n';
	current_position++;
	buf[current_position+1] = '\0';
	current_position++;
	snri_log_binary(0, buf, current_position-1);
}

int intercept_spawn_func(void (*func)(void *), void *data)
{
	/* this takes the place of forking a cpuworker and running cpuworker_main.
	 * func points to cpuworker_main, but we'll implement a version that
	 * works in shadow */
	int *fdarray = data;
	int fd = fdarray[1]; /* this side is ours */
	vtor_cpuworker_init(fd);

	/* now we should be ready to receive events in vtor_cpuworker_readable */
	return 0;
}

void vtor_cpuworker_init(int fd) {
	vtor_cpuworker_tp cpuw = malloc(sizeof(vtor_cpuworker_t));

	cpuw->fd = fd;
	cpuw->onion_key = NULL;
	cpuw->last_onion_key = NULL;

	dup_onion_keys(&(cpuw->onion_key), &(cpuw->last_onion_key));

	/* setup event so we will get a callback */
	event_assign(&(cpuw->read_event), tor_libevent_get_base(), cpuw->fd, EV_READ|EV_PERSIST, vtor_cpuworker_read_cb, cpuw);
	event_add(&(cpuw->read_event), NULL);
}

void vtor_cpuworker_read_cb(int sockd, short ev_types, void * arg) {
	/* taken from cpuworker_main.
	 *
	 * these are blocking calls in Tor. we need to cope, so the approach we
	 * take is that if the first read would block, its still ok. after
	 * that, we fail if the rest of what we expect isnt there.
	 *
	 * FIXME make this completely nonblocking with a state machine.
	 */
	vtor_cpuworker_tp cpuw = arg;

	if(cpuw != NULL) {
		ssize_t r = 0;

		r = recv(cpuw->fd, &(cpuw->question_type), 1, 0);

		if(r < 0) {
			if(errno == EAGAIN) {
				/* dont block! and dont fail! */
				goto ret;
			} else {
				/* true error from shadow network layer */
				log_info(LD_OR,
						 "CPU worker exiting because of error on connection to Tor "
						 "process.");
				log_info(LD_OR,"(Error on %d was %s)",
						cpuw->fd, tor_socket_strerror(tor_socket_errno(cpuw->fd)));
				goto end;
			}
		} else if (r == 0) {
			log_info(LD_OR,
					 "CPU worker exiting because Tor process closed connection "
					 "(either rotated keys or died).");
			goto end;
		}

		/* we got our initial question */

		tor_assert(cpuw->question_type == CPUWORKER_TASK_ONION);

		r = read_all(cpuw->fd, cpuw->tag, TAG_LEN, 1);

		if (r != TAG_LEN) {
		  log_err(LD_BUG,"read tag failed. Exiting.");
		  goto end;
		}

		r = read_all(cpuw->fd, cpuw->question, ONIONSKIN_CHALLENGE_LEN, 1);

		if (r != ONIONSKIN_CHALLENGE_LEN) {
		  log_err(LD_BUG,"read question failed. Exiting.");
		  goto end;
		}

		if (cpuw->question_type == CPUWORKER_TASK_ONION) {
			r = onion_skin_server_handshake(cpuw->question, cpuw->onion_key, cpuw->last_onion_key,
					  cpuw->reply_to_proxy, cpuw->keys, CPATH_KEY_MATERIAL_LEN);

			if (r < 0) {
				/* failure */
				log_debug(LD_OR,"onion_skin_server_handshake failed.");
				*(cpuw->buf) = 0; /* indicate failure in first byte */
				memcpy(cpuw->buf+1,cpuw->tag,TAG_LEN);
				/* send all zeros as answer */
				memset(cpuw->buf+1+TAG_LEN, 0, LEN_ONION_RESPONSE-(1+TAG_LEN));
			} else {
				/* success */
				log_debug(LD_OR,"onion_skin_server_handshake succeeded.");
				cpuw->buf[0] = 1; /* 1 means success */
				memcpy(cpuw->buf+1,cpuw->tag,TAG_LEN);
				memcpy(cpuw->buf+1+TAG_LEN,cpuw->reply_to_proxy,ONIONSKIN_REPLY_LEN);
				memcpy(cpuw->buf+1+TAG_LEN+ONIONSKIN_REPLY_LEN,cpuw->keys,CPATH_KEY_MATERIAL_LEN);
			}

			r = write_all(cpuw->fd, cpuw->buf, LEN_ONION_RESPONSE, 1);

			if (r != LEN_ONION_RESPONSE) {
				log_err(LD_BUG,"writing response buf failed. Exiting.");
				goto end;
			}

			log_debug(LD_OR,"finished writing response.");
		}
	}
ret:
	return;
end:
	if(cpuw != NULL) {
		if (cpuw->onion_key)
			crypto_free_pk_env(cpuw->onion_key);
		if (cpuw->last_onion_key)
			crypto_free_pk_env(cpuw->last_onion_key);
		tor_close_socket(cpuw->fd);
		event_del(&(cpuw->read_event));
		free(cpuw);
	}
}

int intercept_rep_hist_bandwidth_assess() {
//	/* the network address */
//	in_addr_t netaddr;
//	snri_getip(&netaddr);
//
//	/* ask shadow for my configured bandwidth */
//	uint32_t configured_bw = 0;
//	snri_resolve_minbw(netaddr, &configured_bw);

	/* need to convert to bytes. tor will divide the value we return by 1000 and put it in the descriptor. */
	int bw = INT_MAX;
	if((scallion->vtor.bandwidth * 1000) < bw) {
		bw = (scallion->vtor.bandwidth * 1000);
	}
	return bw;
}

//static uint32_t vtor_set_tor_bw(routerinfo_t *router) {
//	uint32_t bw = scallion->vtor.bandwidth * 1000;
//	/* might as well use this opportunity to make sure or BW settings are right */
//	if(router != NULL) {
//		router->bandwidthburst = bw;
//		router->bandwidthrate = bw;
//		router->bandwidthcapacity = bw;
//	}
//	return bw;
//}
//
//uint32_t intercept_router_get_advertised_bandwidth(routerinfo_t *router) {
//	return vtor_set_tor_bw(router);
//}
//
//uint32_t intercept_router_get_advertised_bandwidth_capped(routerinfo_t *router) {
//	return vtor_set_tor_bw(router);
//}

//struct nameserver {
//	int socket;	 /* a connected UDP socket */
//	struct sockaddr_storage address;
//	int failed_times;  /* number of times which we have given this server a chance */
//	int timedout;  /* number of times in a row a request has timed out */
//	struct event event;
//	/* these objects are kept in a circular list */
//	struct nameserver *next, *prev;
//	struct event timeout_event; /* used to keep the timeout for */
//								/* when we next probe this server. */
//								/* Valid if state == 0 */
//	char state;	 /* zero if we think that this server is down */
//	char choked;  /* true if we have an EAGAIN from this server's socket */
//	char write_waiting;	 /* true if we are waiting for EV_WRITE events */
//};
//extern struct nameserver *server_head;
//extern int sockaddr_eq(const struct sockaddr *, const struct sockaddr *, int);
//extern void _evdns_log(int, const char *, ...);
//extern int global_bind_addr_is_set;
//extern struct sockaddr_storage global_bind_address;
//extern socklen_t global_bind_addrlen;
//extern int sockaddr_is_loopback(const struct sockaddr *);
//extern int global_good_nameservers;
//extern const char * debug_ntop(const struct sockaddr *);
//extern void nameserver_prod_callback(int, short, void *);
//extern void nameserver_ready_callback(int, short, void *);
//
//int vtor_intercept__evdns_nameserver_add_impl(const struct sockaddr *address, socklen_t addrlen) {
//	/* first check to see if we already have this nameserver */
//
//	const struct nameserver *server = server_head, *const started_at = server_head;
//	struct nameserver *ns;
//
//	int err = 0;
//	if (server) {
//		do {
//			if (sockaddr_eq(address, (struct sockaddr *)&server->address, 1)) {
//				_evdns_log(0, "Duplicate nameserver.");
//				return 3;
//			}
//			server = server->next;
//		} while (server != started_at);
//	}
//	if (addrlen > (int)sizeof(ns->address)) {
//		_evdns_log(0, "Addrlen %d too long.", (int)addrlen);
//		return 2;
//	}
//
//	ns = (struct nameserver *) _tor_malloc(sizeof(struct nameserver));
//	if (!ns) return -1;
//
//	memset(ns, 0, sizeof(struct nameserver));
//
//	evtimer_set(&ns->timeout_event, nameserver_prod_callback, ns);
//
//	ns->socket = socket(PF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
//	if (ns->socket < 0) { err = 1; goto out1; }
////#ifdef WIN32
////	{
////		u_long nonblocking = 1;
////		ioctlsocket(ns->socket, FIONBIO, &nonblocking);
////	}
////#else
////	fcntl(ns->socket, F_SETFL, O_NONBLOCK);
////#endif
//
//	if (global_bind_addr_is_set &&
//	    !sockaddr_is_loopback((struct sockaddr*)&global_bind_address)) {
//		if (bind(ns->socket, (struct sockaddr *)&global_bind_address,
//				 global_bind_addrlen) < 0) {
//			_evdns_log(0, "Couldn't bind to outgoing address.");
//			err = 2;
//			goto out2;
//		}
//	}
//
//	/* this is a udp connect, it doesnt do any actual connecting */
//	int conn_ret_val = connect(ns->socket, address, addrlen);
//	if (conn_ret_val != 0) {
//		_evdns_log(0, "Couldn't open socket to nameserver.");
//		err = 2;
//		goto out2;
//	}
//
//	memcpy(&ns->address, address, addrlen);
//	ns->state = 1;
//	event_set(&ns->event, ns->socket, EV_READ | EV_PERSIST, nameserver_ready_callback, ns);
//	if (event_add(&ns->event, NULL) < 0) {
//		_evdns_log(0, "Couldn't add event for nameserver.");
//		err = 2;
//		goto out2;
//	}
//
//	_evdns_log(0, "Added nameserver %s", debug_ntop(address));
//
//	/* insert this nameserver into the list of them */
//	if (!server_head) {
//		ns->next = ns->prev = ns;
//		server_head = ns;
//	} else {
//		ns->next = server_head->next;
//		ns->prev = server_head;
//		server_head->next = ns;
//		if (server_head->prev == server_head) {
//			server_head->prev = ns;
//		}
//	}
//
//	global_good_nameservers++;
//
//	return 0;
//
//out2:
//	close(ns->socket);
//out1:
//	do {memset((ns), 0xF0, sizeof(*(ns))); } while(0);
////	tor_free(ns);
//	free(ns);
//	_evdns_log(1, "Unable to add nameserver %s: error %d", debug_ntop(address), err);
//	return err;
//}
