/**
 * Scallion - plug-in for The Shadow Simulator
 *
 * Copyright (c) 2010-2011 Rob Jansen <jansen@cs.umn.edu>
 *
 * This file is part of Scallion.
 *
 * Scallion is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Scallion is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Scallion.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <netinet/in.h>
#include <glib.h>

#include "tor_includes.h"
#include "tor_externs.h"

#include "vtorflow.h"
#include "scallion-plugin.h"

/* run every 5 mins */
#define VTORFLOW_SCHED_PERIOD 60000

extern routerlist_t *router_get_routerlist(void);

/* replacement for torflow in Tor. for now just grab the bandwidth we configured
 * in the DSIM and use that as the measured bandwidth value. since our configured
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
void vtorflow_init_v3bw(void* data) {
	vtor_tp vtor = data;
	/* open the bw file, clearing it if it exists */
	FILE *v3bw = fopen(vtor->v3bw_name, "w");
	if(v3bw == NULL) {
//		snri_log(LOG_WARN, "vtorflow_init_v3bw: v3bandwidth file not updated: can not open file '%s'\n", v3bw_name);
		return;
	}

//	time_t now = time(NULL);
	time_t maxtime = -1;

	/* print time part on first line */
	if(fprintf(v3bw, "%lu\n", maxtime) < 0) {
		/* uhhhh... */
//		snri_log(LOG_WARN, "vtorflow_init_v3bw: v3bandwidth file not updated: can write time '%u' to file '%s'\n", maxtime, v3bw_name);
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
		uint32_t bw = vtor->shadowlibFuncs->getBandwidthFloor(netaddr);

		if(fprintf(v3bw, "node_id=$%s bw=%u\n", node_id, bw) < 0) {
			/* uhhhh... */
//			snri_log(LOG_WARN, "vtorflow_init_v3bw: v3bandwidth file not updated: can write line 'node_id=$%s bw=%u\n' to file '%s'\n", node_id, bw, v3bw_name);
			return;
		}
	}

	fclose(v3bw);

	/* reschedule */
	vtor->shadowlibFuncs->createCallback(&vtorflow_init_v3bw, vtor, VTORFLOW_SCHED_PERIOD);
}
