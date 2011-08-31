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

#ifndef _vci_h
#define _vci_h

/**
 		Virtual Communications Interfacing
*/


#include <glib.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>

#include "vpacket_mgr.h"
#include "vpacket.h"
#include "btree.h"
#include "evtracker.h"
#include "nbdf.h"
#include "context.h"
#include "events.h"
#include "vsocket_mgr.h"
#include "vepoll.h"
#include "vci_event.h"

#define VCI_MAX_DATAGRAM_SIZE 8192 /**< maximum size of a VCI UDP simulated datagram - messages larger than this are dropped */

#define VCI_RLBLTY_100 1000000000 /**< 100% reliability - using fixed pogint math */
#define VCI_RLBLTY_FAC 10000000   /**< scaling factor used for the fixed pogint math */

/**
 * defines how tight we want the distribution around the average value:
 * 0 = normal distribution, 1 = loose, 10 = very tight
 */
#define VCI_NETMODEL_TIGHTNESS_FACTOR 5

enum vci_location {
	LOC_ERROR,
	LOC_SAME_SLAVE_SAME_WORKER,
	LOC_SAME_SLAVE_DIFFERENT_WORKER,
	LOC_DIFFERENT_SLAVE_DIFFERENT_WORKER
};

typedef struct vci_addressing_scheme_t {
	guint slave_mask;

	guint worker_shiftcount;
	guint32 worker_mask;

	guint32 node_shiftcount;
	guint node_randmax;
} vci_addressing_scheme_t, *vci_addressing_scheme_tp;

/**
 * a VCI manager
 *
 * this it holds some quantity of vci-addressable endpogints, along with the mechanisms for ordered
 * delivery
 */
typedef struct vci_mgr_t {
	events_tp events;

	vci_addressing_scheme_tp ascheme;

	guint slave_id;
	guint worker_id;

	/** all nodes this block manages */
	GHashTable *mailboxes;

	/** all virtual networks available */
	GHashTable *networks_by_id;
	GHashTable *networks_by_address;

	vsocket_mgr_tp current_vsocket_mgr;
} vci_mgr_t, *vci_mgr_tp;

/**
 * Any VCI network message uses this message format
 */
typedef struct vci_onretransmit_s {
	in_port_t src_port;
	in_addr_t dst_addr;
	in_port_t dst_port;
	guint32 retransmit_key;
} vci_onretransmit_t, *vci_onretransmit_tp;

typedef struct vci_onnotify_s {
	guint16 sockd;
        vci_mgr_tp vci_mgr;
} vci_onnotify_t, *vci_onnotify_tp;

typedef struct vci_onclose_s {
	in_addr_t src_addr;
	in_port_t src_port;
	in_port_t dst_port;
	guint32 rcv_end;
} vci_onclose_t, *vci_onclose_tp;

/* a single subnet of a larger network */
typedef struct vci_network_t {
	/* network id */
	guint netid;
} vci_network_t, *vci_network_tp;

typedef struct vci_mailbox_t {
	context_provider_tp context_provider;
	vci_network_tp network;
} vci_mailbox_t, *vci_mailbox_tp;

typedef struct vci_scheduling_info_s {
	struct sim_worker_t* worker;
	vci_mgr_tp vci_mgr;
	vci_network_tp src_net;
	vci_network_tp dst_net;
} vci_scheduling_info_t, *vci_scheduling_info_tp;

vci_mgr_tp vci_mgr_create (events_tp events, guint slave_id, guint worker_id, vci_addressing_scheme_tp scheme);
void vci_mgr_destroy(vci_mgr_tp mgr);
in_addr_t vci_create_ip(vci_mgr_tp mgr, gint net_id, context_provider_tp cp);
void vci_free_ip(vci_mgr_tp mgr, in_addr_t addr);
vci_addressing_scheme_tp vci_create_addressing_scheme (gint num_slaves, gint max_wrkr_per_slave);
void vci_destroy_addressing_scheme (vci_addressing_scheme_tp scheme);

in_addr_t vci_ascheme_build_addr(vci_addressing_scheme_tp scheme, guint slave_id, guint worker_id, guint node_id);
guint8 vci_can_share_memory(in_addr_t node);
guint8 vci_get_latency(in_addr_t src_addr, in_addr_t dst_addr,
		guint32* src_to_dst_lat, guint32* dst_to_src_lat);
vci_mailbox_tp vci_get_mailbox(vci_mgr_tp vci_mgr, in_addr_t ip);

guint vci_ascheme_rand_node (vci_addressing_scheme_tp scheme);
guint vci_ascheme_get_worker (vci_addressing_scheme_tp scheme, in_addr_t ip);
guint vci_ascheme_get_slave (vci_addressing_scheme_tp scheme, in_addr_t ip);
guint vci_ascheme_get_node (vci_addressing_scheme_tp scheme, in_addr_t ip);

void vci_schedule_packet(rc_vpacket_pod_tp rc_packet);
void vci_schedule_packet_loopback(rc_vpacket_pod_tp rc_packet, in_addr_t addr);
void vci_schedule_retransmit(rc_vpacket_pod_tp rc_packet, in_addr_t caller_addr);
void vci_schedule_notify(in_addr_t addr, guint16 sockd);
void vci_schedule_uploaded(in_addr_t addr, guint32 nanos_consumed);
void vci_schedule_downloaded(in_addr_t addr, guint32 nanos_consumed);
void vci_schedule_close(in_addr_t caller_addr, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, guint32 rcv_end);
void vci_schedule_dack(in_addr_t addr, guint16 sockd, guint32 ms_delay);
void vci_schedule_poll(in_addr_t addr, vepoll_tp vep, guint32 ms_delay);

void vci_exec_event (vci_mgr_tp mgr, vci_event_tp vci_event);
void vci_destroy_event(events_tp events, vci_event_tp vci_event);
void vci_deposit(vci_mgr_tp vci_mgr, nbdf_tp frame, gint frametype);

vci_network_tp vci_network_create(vci_mgr_tp mgr, guint id);
void vci_track_network(vci_mgr_tp mgr, guint network_id, in_addr_t addr);

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x0100007f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long) -1)
#endif


#endif
