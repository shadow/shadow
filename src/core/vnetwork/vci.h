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


#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>

#include "vpacket_mgr.h"
#include "vpacket.h"
#include "hashtable.h"
#include "btree.h"
#include "evtracker.h"
#include "nbdf.h"
#include "context.h"
#include "events.h"
#include "vsocket_mgr.h"
#include "vepoll.h"

#define VCI_MAX_DATAGRAM_SIZE 8192 /**< maximum size of a VCI UDP simulated datagram - messages larger than this are dropped */

#define VCI_RLBLTY_100 1000000000 /**< 100% reliability - using fixed point math */
#define VCI_RLBLTY_FAC 10000000   /**< scaling factor used for the fixed point math */

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

enum vci_event_code {
	VCI_EC_ONPACKET,
	VCI_EC_ONNOTIFY,
	VCI_EC_ONPOLL,
	VCI_EC_ONDACK,
	VCI_EC_ONUPLOADED,
	VCI_EC_ONDOWNLOADED,
	VCI_EC_ONRETRANSMIT,
	VCI_EC_ONCLOSE
};

typedef struct vci_ondownloaded_t {
} vci_ondownloaded_t, *vci_ondownloaded_tp;

typedef struct vci_onuploaded_t {
} vci_onuploaded_t, *vci_onuploaded_tp;

/**
 * Any VCI network message uses this message format
 */
typedef struct vci_onpacket_s {
	rc_vpacket_pod_tp rc_pod;
} vci_onpacket_t, *vci_onpacket_tp;

typedef struct vci_onretransmit_s {
	in_port_t src_port;
	in_addr_t dst_addr;
	in_port_t dst_port;
	uint32_t retransmit_key;
} vci_onretransmit_t, *vci_onretransmit_tp;

typedef struct vci_onnotify_s {
	uint16_t sockd;
} vci_onnotify_t, *vci_onnotify_tp;

typedef struct vci_onpoll_s {
	vepoll_tp vep;
} vci_onpoll_t, *vci_onpoll_tp;

typedef struct vci_ondack_s {
	uint16_t sockd;
} vci_ondack_t, *vci_ondack_tp;

typedef struct vci_onclose_s {
	in_addr_t src_addr;
	in_port_t src_port;
	in_port_t dst_port;
	uint32_t rcv_end;
} vci_onclose_t, *vci_onclose_tp;

typedef struct vci_event_s {
	enum vci_event_code code;
	ptime_t deliver_time;
	in_addr_t node_addr;
	in_addr_t owner_addr;
	uint64_t cpu_delay_position;
	void* payload;
} vci_event_t, *vci_event_tp;

typedef struct vci_addressing_scheme_t {
	unsigned int slave_mask;

	unsigned int worker_shiftcount;
	uint32_t worker_mask;

	uint32_t node_shiftcount;
	unsigned int node_randmax;
} vci_addressing_scheme_t, *vci_addressing_scheme_tp;

/* a single subnet of a larger network */
typedef struct vci_network_t {
	/* network id */
	unsigned int netid;
} vci_network_t, *vci_network_tp;

typedef struct vci_mailbox_t {
	context_provider_tp context_provider;
	vci_network_tp network;
} vci_mailbox_t, *vci_mailbox_tp;

/**
 * a VCI manager
 *
 * this it holds some quantity of vci-addressable endpoints, along with the mechanisms for ordered
 * delivery
 */
typedef struct vci_mgr_t {
	events_tp events;

	vci_addressing_scheme_tp ascheme;

	unsigned int slave_id;
	unsigned int worker_id;

	/** all nodes this block manages */
	hashtable_tp mailboxes;

	/** all virtual networks available */
	hashtable_tp networks_by_id;
	hashtable_tp networks_by_address;

	vsocket_mgr_tp current_vsocket_mgr;
} vci_mgr_t, *vci_mgr_tp;

typedef struct vci_scheduling_info_s {
	struct sim_worker_t* worker;
	vci_mgr_tp vci_mgr;
	vci_network_tp src_net;
	vci_network_tp dst_net;
} vci_scheduling_info_t, *vci_scheduling_info_tp;

vci_mgr_tp vci_mgr_create (events_tp events, unsigned int slave_id, unsigned int worker_id, vci_addressing_scheme_tp scheme);
void vci_mgr_destroy(vci_mgr_tp mgr);
in_addr_t vci_create_ip(vci_mgr_tp mgr, int net_id, context_provider_tp cp);
void vci_free_ip(vci_mgr_tp mgr, in_addr_t addr);
vci_addressing_scheme_tp vci_create_addressing_scheme (int num_slaves, int max_wrkr_per_slave);
void vci_destroy_addressing_scheme (vci_addressing_scheme_tp scheme);

in_addr_t vci_ascheme_build_addr(vci_addressing_scheme_tp scheme, unsigned int slave_id, unsigned int worker_id, unsigned int node_id);
uint8_t vci_can_share_memory(in_addr_t node);
uint8_t vci_get_latency(in_addr_t src_addr, in_addr_t dst_addr,
		uint32_t* src_to_dst_lat, uint32_t* dst_to_src_lat);

unsigned int vci_ascheme_rand_node (vci_addressing_scheme_tp scheme);
unsigned int vci_ascheme_get_worker (vci_addressing_scheme_tp scheme, in_addr_t ip);
unsigned int vci_ascheme_get_slave (vci_addressing_scheme_tp scheme, in_addr_t ip);
unsigned int vci_ascheme_get_node (vci_addressing_scheme_tp scheme, in_addr_t ip);

void vci_schedule_packet(rc_vpacket_pod_tp rc_packet);
void vci_schedule_packet_loopback(rc_vpacket_pod_tp rc_packet, in_addr_t addr);
void vci_schedule_retransmit(rc_vpacket_pod_tp rc_packet, in_addr_t caller_addr);
void vci_schedule_notify(in_addr_t addr, uint16_t sockd);
void vci_schedule_uploaded(in_addr_t addr, uint32_t nanos_consumed);
void vci_schedule_downloaded(in_addr_t addr, uint32_t nanos_consumed);
void vci_schedule_close(in_addr_t caller_addr, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, uint32_t rcv_end);
void vci_schedule_dack(in_addr_t addr, uint16_t sockd, uint32_t ms_delay);
void vci_schedule_poll(in_addr_t addr, vepoll_tp vep, uint32_t ms_delay);

void vci_exec_event (vci_mgr_tp mgr, vci_event_tp vci_event);
void vci_destroy_event(vci_event_tp vci_event);
void vci_deposit(vci_mgr_tp vci_mgr, nbdf_tp frame, int frametype);

vci_network_tp vci_network_create(vci_mgr_tp mgr, unsigned int id);
void vci_track_network(vci_mgr_tp mgr, unsigned int network_id, in_addr_t addr);

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x0100007f
#endif

#ifndef INADDR_NONE
#define INADDR_NONE ((unsigned long) -1)
#endif


#endif
