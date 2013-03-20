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

#include "shd-scallion.h"

/* replacement for torflow in Tor. for now just grab the bandwidth we configured
 * in the XML and use that as the measured bandwidth value. since our configured
 * bandwidth doesnt change over time, this could just be run once (by setting the
 * time far in the future so the file is not seen as outdated). but we need to
 * run it after all routers are loaded, so its best to re-run periodically.
 *
 * eventually we will want an option to run something similar to the actual
 * torflow scripts that download files over Tor and computes bandwidth values.
 * in that case it needs to run more often to keep monitoring the actual state
 * of the network.
 *
 * torflow writes a few things to the v3bwfile. all Tor currently uses is:
 *
 * 0123456789
 * node_id=$0123456789ABCDEF0123456789ABCDEF01234567 bw=12345
 * ...
 *
 * where 0123456789 is the time, 0123456789ABCDEF0123456789ABCDEF01234567 is
 * the relay's fingerprint, and 12345 is the measured bandwidth in ?.
 */
void scalliontor_init_v3bw(ScallionTor* stor) {
	/* open the bw file, clearing it if it exists */
	FILE *v3bw = fopen(stor->v3bw_name, "w");
	if(v3bw == NULL) {
		stor->shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
				"v3bandwidth file not updated: can not open file '%s'\n", stor->v3bw_name);
		return;
	}

	time_t maxtime = -1;

	/* print time part on first line */
	if(fprintf(v3bw, "%lu\n", maxtime) < 0) {
		/* uhhhh... */
		stor->shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
		"v3bandwidth file not updated: can write time '%u' to file '%s'\n", maxtime, stor->v3bw_name);
		return;
	}

	routerlist_t *rlist = router_get_routerlist();
	routerinfo_t *rinfo;

	/* print an entry for each router */
	for (int i=0; i < smartlist_len(rlist->routers); i++) {
		rinfo = smartlist_get(rlist->routers, i);

		/* get the fingerprint from its digest */
		char node_id[HEX_DIGEST_LEN+1];
		base16_encode(node_id, HEX_DIGEST_LEN+1, rinfo->cache_info.identity_digest, DIGEST_LEN);

		/* the network address */
		in_addr_t netaddr = htonl(rinfo->addr);

		/* ask shadow for this node's configured bandwidth */
		guint bwdown = 0, bwup = 0;
		stor->shadowlibFuncs->getBandwidth(netaddr, &bwdown, &bwup);

		/* XXX careful here! shadow bandwidth may be different than the consensus
		 * right now i believe this v3bw file is not used to compute the consensus
		 * "w Bandwidth" line, and
		 * intercept_rep_hist_bandwidth_assess and
		 * intercept_router_get_advertised_bandwidth_capped
		 * takes care of things. so leave it for now.
		 */
		guint bw = MIN(bwup, bwdown);

		if(fprintf(v3bw, "node_id=$%s bw=%u\n", node_id, bw) < 0) {
			/* uhhhh... */
			stor->shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"v3bandwidth file not updated: can write line 'node_id=$%s bw=%u\n' to file '%s'\n", node_id, bw, stor->v3bw_name);
			return;
		}
	}

	fclose(v3bw);

	/* reschedule */
	stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)scalliontor_init_v3bw, (gpointer)stor, VTORFLOW_SCHED_PERIOD);
}

void scalliontor_free(ScallionTor* stor) {
	tor_cleanup();
	g_free(stor);
}

static void _scalliontor_secondCallback(ScallionTor* stor) {
	scalliontor_notify(stor);

	/* call Tor's second elapsed function */
	second_elapsed_callback(NULL, NULL);

	/* make sure we handle any event creations that happened in Tor */
	scalliontor_notify(stor);

	/* schedule the next callback */
	if(stor) {
		stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)_scalliontor_secondCallback,
				stor, 1000);
	}
}

#ifdef SCALLION_DOREFILLCALLBACKS
static void _scalliontor_refillCallback(ScallionTor* stor) {
	scalliontor_notify(stor);

	/* call Tor's refill function */
	refill_callback(NULL, NULL);

        /* notify stream BW events */
        control_event_stream_bandwidth_used();

	/* make sure we handle any event creations that happened in Tor */
	scalliontor_notify(stor);

	/* schedule the next callback */
	if(stor) {
		stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)_scalliontor_refillCallback,
				stor, stor->refillmsecs);
	}
}
#endif

gint scalliontor_start(ScallionTor* stor, gint argc, gchar *argv[]) {
	time_t now = time(NULL);

	update_approx_time(now);
	tor_threads_init();
	init_logging();

	if (tor_init(argc, argv) < 0) {
		return -1;
	}

	  /* load the private keys, if we're supposed to have them, and set up the
	   * TLS context. */
	gpointer idkey;
#ifdef SCALLION_DOREFILLCALLBACKS // FIXME this doesnt change in 0.2.3.5-alpha like SCALLION_DOREFILL is meant to (not sure when it changed)
	idkey = client_identitykey;
#else
	idkey = identitykey;
#endif
    if (idkey == NULL) {
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
	directory_info_has_arrived(now, 1);

	/* !note that scallion intercepts the cpuworker functionality (rob) */
	if (server_mode(get_options())) {
		/* launch cpuworkers. Need to do this *after* we've read the onion key. */
		cpu_init();
	}

	/* set up once-a-second callback. */
	if (! second_timer) {
//		struct timeval one_second;
//		one_second.tv_sec = 1;
//		one_second.tv_usec = 0;
//
//		second_timer = periodic_timer_new(tor_libevent_get_base(),
//										  &one_second,
//										  second_elapsed_callback,
//										  NULL);
//		tor_assert(second_timer);

		_scalliontor_secondCallback(stor);
	}


#ifdef SCALLION_DOREFILLCALLBACKS
#ifndef USE_BUFFEREVENTS
  if (!refill_timer) {
    int msecs = get_options()->TokenBucketRefillInterval;
//    struct timeval refill_interval;
//
//    refill_interval.tv_sec =  msecs/1000;
//    refill_interval.tv_usec = (msecs%1000)*1000;
//
//    refill_timer = periodic_timer_new(tor_libevent_get_base(),
//                                      &refill_interval,
//                                      refill_callback,
//                                      NULL);
//    tor_assert(refill_timer);
    stor->refillmsecs = msecs;
	_scalliontor_refillCallback(stor);
  }
#endif
#endif

    /* run the startup events */
    scalliontor_notify(stor);

	return 0;
}

static GString* _scalliontor_getHomePath(gchar* path) {
	GString* sbuffer = g_string_new("");
	if(g_ascii_strncasecmp(path, "~", 1) == 0) {
		/* replace ~ with home directory */
		const gchar* home = g_get_home_dir();
		g_string_append_printf(sbuffer, "%s%s", home, path+1);
	} else {
		g_string_append_printf(sbuffer, "%s", path);
	}
	return sbuffer;
}

ScallionTor* scalliontor_new(ShadowFunctionTable* shadowlibFuncs, char* hostname, enum vtor_nodetype type,
		char* bandwidth, char* bwrate, char* bwburst, char* torrc_path, char* datadir_path, char* geoip_path) {
	ScallionTor* stor = g_new0(ScallionTor, 1);
	stor->shadowlibFuncs = shadowlibFuncs;

	stor->type = type;
	stor->bandwidth = (unsigned int) atoi(bandwidth);

	/* make sure the paths are absolute */
	GString* torrcBuffer = _scalliontor_getHomePath(torrc_path);
	GString* datadirBuffer = _scalliontor_getHomePath(datadir_path);
	GString* geoipBuffer = _scalliontor_getHomePath(geoip_path);

	/* default args */
	char *config[25];
	config[0] = "tor";
	config[1] = "--Address";
	config[2] = hostname;
	config[3] = "-f";
	config[4] = torrcBuffer->str;
	config[5] = "--DataDirectory";
	config[6] = datadirBuffer->str;
	config[7] = "--GeoIPFile";
	config[8] = geoipBuffer->str;
	config[9] = "--BandwidthRate";
	config[10] = bwrate;
	config[11] = "--BandwidthBurst";
	config[12] = bwburst;

	gchar* nickname = g_strdup(hostname);
	while(1) {
		gchar* dot = g_strstr_len((const gchar*)nickname, -1, ".");
		if(dot != NULL) {
			*dot = 'x';
		} else {
			break;
		}
	}

	config[13] = "--Nickname";
	config[14] = nickname;

	config[15] = "--ControlPort";
    config[16] = "9051";
    config[17] = "--ControlListenAddress";
    config[18] = "127.0.0.1";
    config[19] = "--ControlListenAddress";
    config[20] = hostname;
    config[21] = "HashedControlPassword";
    config[22] = "16:25662F13DA7881D46091AB96726A8E5245CBF98BA6961A5B8C9CEEBB25";

	int num_args = 23;
	/* additional args */
	if(stor->type == VTOR_DIRAUTH) {
		num_args += 2;
		if(snprintf(stor->v3bw_name, 255, "%s/dirauth.v3bw", datadir_path) >= 255) {
			stor->shadowlibFuncs->log(G_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"data directory path is too long and was truncated to '%s'\n", stor->v3bw_name);
		}
		config[23] = "--V3BandwidthsFile";
		config[24] = stor->v3bw_name;
	}

	scallion.stor = stor;
	scalliontor_start(stor, num_args, config);

	if(stor->type == VTOR_DIRAUTH) {
		/* run torflow now, it will schedule itself as needed */
		scalliontor_init_v3bw(stor);
	}

	g_string_free(torrcBuffer, TRUE);
	g_string_free(datadirBuffer, TRUE);
	g_string_free(geoipBuffer, TRUE);
	g_free(nickname);

	return stor;
}

void scalliontor_notify(ScallionTor* stor) {
	update_approx_time(time(NULL));

	/* tell libevent to check epoll and activate the ready sockets without blocking */
	event_base_loop(tor_libevent_get_base(), EVLOOP_NONBLOCK);
}

/*
 * normally tor calls event_base_loopexit so control returns from the libevent
 * event loop back to the tor main loop. tor then activates "linked" socket
 * connections before returning back to the libevent event loop.
 *
 * we hijack and use the libevent loop in nonblock mode, so when tor calls
 * the loopexit, we basically just need to do the linked connection activation.
 * that is extracted to scalliontor_loopexitCallback, which we need to execute
 * as a callback so we don't invoke event_base_loop while it is currently being
 * executed. */
static void scalliontor_loopexitCallback(ScallionTor* stor) {
	update_approx_time(time(NULL));

	scalliontor_notify(stor);

	while(1) {
		/* All active linked conns should get their read events activated. */
		SMARTLIST_FOREACH(active_linked_connection_lst, connection_t *, conn,
				event_active(conn->read_event, EV_READ, 1));

		/* if linked conns are still active, enter libevent loop using EVLOOP_ONCE */
		called_loop_once = smartlist_len(active_linked_connection_lst) ? 1 : 0;
		if(called_loop_once) {
			event_base_loop(tor_libevent_get_base(), EVLOOP_ONCE|EVLOOP_NONBLOCK);
		} else {
			/* linked conns are done */
			break;
		}
	}

	/* make sure we handle any new events caused by the linked conns */
	scalliontor_notify(stor);
}
void scalliontor_loopexit(ScallionTor* stor) {
	stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)scalliontor_loopexitCallback, (gpointer)stor, 1);
}

/* return -1 to kill, 0 for EAGAIN, bytes read/written for success */
static int scalliontor_checkIOResult(vtor_cpuworker_tp cpuw, int ioResult) {
	g_assert(cpuw);

	if(ioResult < 0) {
		if(errno == EAGAIN) {
			/* dont block! and dont fail! */
			return 0;
		} else {
			/* true error from shadow network layer */
			log_info(LD_OR,
					 "CPU worker exiting because of error on connection to Tor "
					 "process.");
			log_info(LD_OR,"(Error on %d was %s)",
					cpuw->fd, tor_socket_strerror(tor_socket_errno(cpuw->fd)));
			return -1;
		}
	} else if (ioResult == 0) {
		log_info(LD_OR,
				 "CPU worker exiting because Tor process closed connection "
				 "(either rotated keys or died).");
		return -1;
	}

	return ioResult;
}

#ifdef SCALLION_USEV2CPUWORKER
void scalliontor_readCPUWorkerCallback(int sockd, short ev_types, void * arg) {
	vtor_cpuworker_tp cpuw = arg;
	g_assert(cpuw);

	int ioResult = 0;
	int action = 0;

enter:
	switch (cpuw->state) {
	case CPUW_V2_READ: {
		ioResult = 0;
		action = 1;
		int bytesNeeded = sizeof(cpuw->req);

		/* look for request */
		while(action > 0 && cpuw->offset < bytesNeeded) {
			ioResult = recv(cpuw->fd, &(cpuw->req)+cpuw->offset, bytesNeeded-cpuw->offset, 0);

			action = scalliontor_checkIOResult(cpuw, ioResult);
			if(action == -1) goto end; // error, kill ourself
			else if(action == 0) goto ret; // EAGAIN

			/* read some bytes */
			cpuw->offset += action;
		}

		/* we got what we needed, assert this */
		if (cpuw->offset != bytesNeeded) {
		  log_err(LD_BUG,"read tag failed. Exiting.");
		  goto end;
		}

		/* got request, process it */
		cpuw->state = CPUW_V2_PROCESS;
		cpuw->offset = 0;
		goto enter;
	}

	case CPUW_V2_PROCESS: {
		tor_assert(cpuw->req.magic == CPUWORKER_REQUEST_MAGIC);

		memset(&cpuw->rpl, 0, sizeof(cpuw->rpl));

		if (cpuw->req.task == CPUWORKER_TASK_ONION) {
			const create_cell_t *cc = &cpuw->req.create_cell;
			created_cell_t *cell_out = &cpuw->rpl.created_cell;
			int n;
			n = onion_skin_server_handshake(cc->handshake_type, cc->onionskin,
					cc->handshake_len, &cpuw->onion_keys, cell_out->reply,
					cpuw->rpl.keys, CPATH_KEY_MATERIAL_LEN,
					cpuw->rpl.rend_auth_material);
			if (n < 0) {
				/* failure */
				log_debug(LD_OR, "onion_skin_server_handshake failed.");
				memset(&cpuw->rpl, 0, sizeof(cpuw->rpl));
				memcpy(cpuw->rpl.tag, cpuw->req.tag, TAG_LEN);
				cpuw->rpl.success = 0;
			} else {
				/* success */
				log_debug(LD_OR, "onion_skin_server_handshake succeeded.");
				memcpy(cpuw->rpl.tag, cpuw->req.tag, TAG_LEN);
				cell_out->handshake_len = n;
				switch (cc->cell_type) {
				case CELL_CREATE:
					cell_out->cell_type = CELL_CREATED;
					break;
				case CELL_CREATE2:
					cell_out->cell_type = CELL_CREATED2;
					break;
				case CELL_CREATE_FAST:
					cell_out->cell_type = CELL_CREATED_FAST;
					break;
				default:
					tor_assert(0);
					goto end;
				}
				cpuw->rpl.success = 1;
			}
			cpuw->rpl.magic = CPUWORKER_REPLY_MAGIC;
		} else if (cpuw->req.task == CPUWORKER_TASK_SHUTDOWN) {
			log_info(LD_OR, "Clean shutdown: exiting");
			goto end;
		}

		cpuw->state = CPUW_V2_WRITE;
		goto enter;
	}

	case CPUW_V2_WRITE: {
		ioResult = 0;
		action = 1;
		int bytesNeeded = sizeof(cpuw->rpl);

		while(action > 0 && cpuw->offset < bytesNeeded) {
			ioResult = send(cpuw->fd, &cpuw->rpl+cpuw->offset, bytesNeeded-cpuw->offset, 0);

			action = scalliontor_checkIOResult(cpuw, ioResult);
			if(action == -1) goto end;
			else if(action == 0) goto ret;

			/* wrote some bytes */
			cpuw->offset += action;
		}

		/* we wrote what we needed, assert this */
		if (cpuw->offset != bytesNeeded) {
			log_err(LD_BUG,"writing response buf failed. Exiting.");
			goto end;
		}

		log_debug(LD_OR,"finished writing response.");

		cpuw->state = CPUW_V2_READ;
		cpuw->offset = 0;
		memwipe(&cpuw->req, 0, sizeof(cpuw->req));
		memwipe(&cpuw->rpl, 0, sizeof(cpuw->req));
		goto enter;
	}
	}

ret:
	return;

end:
	if (cpuw != NULL) {
		memwipe(&cpuw->req, 0, sizeof(cpuw->req));
		memwipe(&cpuw->rpl, 0, sizeof(cpuw->req));
		release_server_onion_keys(&cpuw->onion_keys);
		tor_close_socket(cpuw->fd);
		event_del(&(cpuw->read_event));
		free(cpuw);
	}
}
#else
void scalliontor_readCPUWorkerCallback(int sockd, short ev_types, void * arg) {
	/* adapted from cpuworker_main.
	 *
	 * these are blocking calls in Tor. we need to cope, so the approach we
	 * take is that if the first read would block, its ok. after that, we
	 * continue through the state machine until we are able to read and write
	 * everything we need to, then reset and start with the next question.
	 *
	 * this is completely nonblocking with the state machine.
	 */
	vtor_cpuworker_tp cpuw = arg;
	g_assert(cpuw);

	int ioResult = 0;
	int action = 0;

enter:

	switch(cpuw->state) {
		case CPUW_READTYPE: {
			ioResult = 0;

			/* get the type of question */
			ioResult = recv(cpuw->fd, &(cpuw->question_type), 1, 0);

			action = scalliontor_checkIOResult(cpuw, ioResult);
			if(action == -1) goto kill;
			else if(action == 0) goto exit;

			/* we got our initial question type */
			tor_assert(cpuw->question_type == CPUWORKER_TASK_ONION);

			cpuw->state = CPUW_READTAG;
			goto enter;
		}

		case CPUW_READTAG: {
			ioResult = 0;
			action = 1;
			int bytesNeeded = TAG_LEN;

			while(action > 0 && cpuw->offset < bytesNeeded) {
				ioResult = recv(cpuw->fd, cpuw->tag+cpuw->offset, bytesNeeded-cpuw->offset, 0);

				action = scalliontor_checkIOResult(cpuw, ioResult);
				if(action == -1) goto kill;
				else if(action == 0) goto exit;

				/* read some bytes */
				cpuw->offset += action;
			}

			/* we got what we needed, assert this */
			if (cpuw->offset != TAG_LEN) {
			  log_err(LD_BUG,"read tag failed. Exiting.");
			  goto kill;
			}

			cpuw->state = CPUW_READCHALLENGE;
			cpuw->offset = 0;
			goto enter;
		}

		case CPUW_READCHALLENGE: {
			ioResult = 0;
			action = 1;
			int bytesNeeded = ONIONSKIN_CHALLENGE_LEN;

			while(action > 0 && cpuw->offset < bytesNeeded) {
				ioResult = recv(cpuw->fd, cpuw->question+cpuw->offset, bytesNeeded-cpuw->offset, 0);

				action = scalliontor_checkIOResult(cpuw, ioResult);
				if(action == -1) goto kill;
				else if(action == 0) goto exit;

				/* read some bytes */
				cpuw->offset += action;
			}

			/* we got what we needed, assert this */
			if (cpuw->offset != ONIONSKIN_CHALLENGE_LEN) {
			  log_err(LD_BUG,"read question failed. got %i bytes, expecting %i bytes. Exiting.", cpuw->offset, ONIONSKIN_CHALLENGE_LEN);
			  goto kill;
			}

			cpuw->state = CPUW_PROCESS;
			cpuw->offset = 0;
			goto enter;
		}

		case CPUW_PROCESS: {
			if (cpuw->question_type != CPUWORKER_TASK_ONION) {
				log_debug(LD_OR,"unknown CPU worker question type. ignoring...");
				cpuw->state = CPUW_READTYPE;
				cpuw->offset = 0;
				goto exit;
			}


			int r = onion_skin_server_handshake(cpuw->question, cpuw->onion_key, cpuw->last_onion_key,
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

			cpuw->state = CPUW_WRITERESPONSE;
			cpuw->offset = 0;
			goto enter;
		}

		case CPUW_WRITERESPONSE: {
			ioResult = 0;
			action = 1;
			int bytesNeeded = LEN_ONION_RESPONSE;

			while(action > 0 && cpuw->offset < bytesNeeded) {
				ioResult = send(cpuw->fd, cpuw->buf+cpuw->offset, bytesNeeded-cpuw->offset, 0);

				action = scalliontor_checkIOResult(cpuw, ioResult);
				if(action == -1) goto kill;
				else if(action == 0) goto exit;

				/* wrote some bytes */
				cpuw->offset += action;
			}

			/* we wrote what we needed, assert this */
			if (cpuw->offset != LEN_ONION_RESPONSE) {
				log_err(LD_BUG,"writing response buf failed. Exiting.");
				goto kill;
			}

			log_debug(LD_OR,"finished writing response.");

			cpuw->state = CPUW_READTYPE;
			cpuw->offset = 0;
			goto enter;
		}

		default: {
			log_err(LD_BUG,"unknown CPU worker state. Exiting.");
			goto kill;
		}
	}

exit:
	return;

kill:
	if(cpuw != NULL) {
		if (cpuw->onion_key)
			crypto_pk_free(cpuw->onion_key);
		if (cpuw->last_onion_key)
			crypto_pk_free(cpuw->last_onion_key);
		tor_close_socket(cpuw->fd);
		event_del(&(cpuw->read_event));
		free(cpuw);
	}
}
#endif

void scalliontor_newCPUWorker(ScallionTor* stor, int fd) {
	g_assert(stor);
	if(stor->cpuw) {
		g_free(stor->cpuw);
	}

	vtor_cpuworker_tp cpuw = calloc(1, sizeof(vtor_cpuworker_t));

	cpuw->fd = fd;
	cpuw->state = CPUW_READTYPE;

#ifdef SCALLION_USEV2CPUWORKER
	setup_server_onion_keys(&(cpuw->onion_keys));
#else
	dup_onion_keys(&(cpuw->onion_key), &(cpuw->last_onion_key));
#endif

	/* setup event so we will get a callback */
	event_assign(&(cpuw->read_event), tor_libevent_get_base(), cpuw->fd, EV_READ|EV_PERSIST, scalliontor_readCPUWorkerCallback, cpuw);
	event_add(&(cpuw->read_event), NULL);
}

static ScallionTor* scalliontor_getPointer() {
	return scallion.stor;
}

/*
 * Tor function interceptions
 */

int intercept_event_base_loopexit(struct event_base * base, const struct timeval * t) {
	ScallionTor* stor = scalliontor_getPointer();
	g_assert(stor);

	scalliontor_loopexit(stor);
	return 0;
}

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
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);
	timeval->tv_sec = tp.tv_sec;
	timeval->tv_usec = tp.tv_nsec/1000;
}

void intercept_logv(int severity, uint32_t domain, const char *funcname,
     const char *format, va_list ap) {
	char* sev_str = NULL;
	const size_t buflen = 10024;
	char buf[buflen];
	size_t current_position = 0;

	/* Call assert, not tor_assert, since tor_assert calls log on failure. */
	assert(format);

	GLogLevelFlags level;

	switch (severity) {
		case LOG_DEBUG:
			sev_str = "debug";
			level = G_LOG_LEVEL_DEBUG;
		break;

		case LOG_INFO:
			sev_str = "info";
			level = G_LOG_LEVEL_INFO;
		break;

		case LOG_NOTICE:
			sev_str = "notice";
			level = G_LOG_LEVEL_MESSAGE;
		break;

		case LOG_WARN:
			sev_str = "warn";
			level = G_LOG_LEVEL_WARNING;
		break;

		case LOG_ERR:
			sev_str = "err";
			level = G_LOG_LEVEL_ERROR;
		break;

		default:
			sev_str = "UNKNOWN";
			level = G_LOG_LEVEL_DEBUG;
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

	buf[current_position] = '\0';
	current_position++;

	ScallionTor* stor = scalliontor_getPointer();
	stor->shadowlibFuncs->log(level, __FUNCTION__, buf);

	for(GList *iter = stor->logfiles; iter; iter = g_list_next(iter)) {
        vtor_logfile_tp lf = iter->data;
        lf->callback(severity, domain, buf);
    }
}

int intercept_spawn_func(void (*func)(void *), void *data)
{
	ScallionTor* stor = scalliontor_getPointer();
	g_assert(stor);

	/* this takes the place of forking a cpuworker and running cpuworker_main.
	 * func points to cpuworker_main, but we'll implement a version that
	 * works in shadow */
	int *fdarray = data;
	int fd = fdarray[1]; /* this side is ours */

	scalliontor_newCPUWorker(stor, fd);

	/* now we should be ready to receive events in vtor_cpuworker_readable */
	return 0;
}

/* this function is where the relay will return its bandwidth and send to auth */
int intercept_rep_hist_bandwidth_assess() {
	ScallionTor* stor = scalliontor_getPointer();
	g_assert(stor);

	/* return BW in bytes. tor will divide the value we return by 1000 and put it in the descriptor. */
	return stor->bandwidth;
}

/* this is the authority function to compute the consensus "w Bandwidth" line */
uint32_t intercept_router_get_advertised_bandwidth_capped(const routerinfo_t *router)
{
  /* this is what the relay told us. dont worry about caps, since this bandwidth
   * is authoritative in our sims */
  return router->bandwidthcapacity;
}

/* for the tor control, need to know all the callbacks where log messages get sent */
int intercept_add_callback_log(const log_severity_list_t *severity, log_callback cb) {
    ScallionTor* stor = scalliontor_getPointer();
    g_assert(stor);

    vtor_logfile_tp lf = g_new0(vtor_logfile_t, 1);
    lf->fd = -1;
    lf->severities = g_memdup(severity, sizeof(log_severity_list_t));
    lf->callback = cb;

    stor->logfiles = g_list_append(stor->logfiles, lf);

    return 0;
}

int intercept_crypto_global_cleanup(void) {
	/* FIXME: we need to clean up all of the node-specific state while only
	 * calling the global openssl cleanup funcs once.
	 *
	 * node-specific state can be cleaned up here
	 *
	 * other stuff may be able to be cleaned up in g_module_unload(), but that
	 * is called once per thread which still may piss off openssl. */
	return 0;
}
