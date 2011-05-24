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

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "global.h"
#include "vci.h"
#include "vsocket_mgr.h"
#include "vtransport_mgr.h"
#include "vtransport.h"
#include "vtcp.h"
#include "vpacket_mgr.h"
#include "vpacket.h"
#include "shmcabinet.h"
#include "sysconfig.h"
#include "log.h"
#include "rand.h"
#include "context.h"
#include "module.h"
#include "routing.h"
#include "netconst.h"
#include "sim.h"
#include "simnet_graph.h"
#include "vepoll.h"

static void vci_free_network(void * vnet, int netid) ;
static void vci_free_mailbox(void * vmbox, int laddr);

static vci_scheduling_info_tp vci_get_scheduling_info(in_addr_t src_addr, in_addr_t dst_addr);
static enum vci_location vci_get_relative_location(in_addr_t relative_to);
static vci_mailbox_tp vci_get_mailbox(vci_mgr_tp vci_mgr, in_addr_t ip);

static vsocket_mgr_tp vci_enter_vnetwork_context(vci_mgr_tp vci_mgr, in_addr_t addr);
static void vci_exit_vnetwork_context(vci_mgr_tp vci_mgr);
static vci_event_tp vci_create_event(vci_mgr_tp vci_mgr, enum vci_event_code code, ptime_t deliver_time,
		in_addr_t node_addr, void* payload);
static void vci_schedule_event(events_tp events, vci_event_tp vci_event);
static void vci_schedule_transferred(in_addr_t addr, uint32_t msdelay, enum vci_event_code code);

static nbdf_tp vci_construct_pipecloud_packet_frame(ptime_t time, vpacket_tp packet);

static vci_event_tp vci_decode(vci_mgr_tp vci_mgr, nbdf_tp frame, int frametype);


vci_addressing_scheme_tp vci_create_addressing_scheme (int num_slaves, int max_wrkr_per_slave) {
	vci_addressing_scheme_tp scheme;
	unsigned int slave_bit_count;
	unsigned int worker_bit_count;

	scheme = malloc(sizeof(*scheme));
	if(!scheme)
		printfault(EXIT_NOMEM, "vci_create_addressing_scheme: Out of memory");

	slave_bit_count = ceil(log(num_slaves) / log(2));
	scheme->slave_mask = (uint32_t) pow(2, slave_bit_count) - 1;

	worker_bit_count = ceil(log(max_wrkr_per_slave) / log(2));
	scheme->worker_mask = (uint32_t) pow(2, worker_bit_count) - 1;
	scheme->worker_mask <<= slave_bit_count;
	scheme->worker_shiftcount = slave_bit_count;

	scheme->node_shiftcount = worker_bit_count + slave_bit_count;
	scheme->node_randmax = pow(2, 32 - scheme->node_shiftcount) - 1;

	return scheme;
}

void vci_destroy_addressing_scheme (vci_addressing_scheme_tp scheme) {
	free(scheme);
}

unsigned int vci_ascheme_get_worker (vci_addressing_scheme_tp scheme, in_addr_t ip) {
	return ((ip & scheme->worker_mask) >> scheme->worker_shiftcount);
}

unsigned int vci_ascheme_get_slave (vci_addressing_scheme_tp scheme, in_addr_t ip) {
	return (ip & scheme->slave_mask);
}

unsigned int vci_ascheme_get_node (vci_addressing_scheme_tp scheme, in_addr_t ip) {
	return (ip >> scheme->node_shiftcount);
}

unsigned int vci_ascheme_rand_node (vci_addressing_scheme_tp scheme) {
	unsigned int node;
	unsigned char high_order;

	do {
		node = dvn_rand_fast(scheme->node_randmax);
		high_order = node >> (24 - scheme->node_shiftcount);
	} while(high_order == 0 || high_order == 0xff);

	return node;
}

in_addr_t vci_ascheme_build_addr(vci_addressing_scheme_tp scheme, unsigned int slave_id, unsigned int worker_id, unsigned int node_id) {
	return slave_id + (worker_id << scheme->worker_shiftcount) + (node_id << scheme->node_shiftcount);
	//return node_id + (worker_id << scheme->worker_shiftcount) + (slave_id  << scheme->slave_shiftcount);
}

/* create a new node inside net_id network */
in_addr_t vci_create_ip(vci_mgr_tp mgr, int net_id, context_provider_tp cp) {
	uint32_t laddr;
	in_addr_t addr;
	vci_mailbox_tp mbox;

	vci_network_tp net = hashtable_get(mgr->networks_by_id, net_id);
	if(!net)
		return INADDR_NONE;

	do {
		laddr = vci_ascheme_rand_node(mgr->ascheme);
	} while(hashtable_get(mgr->mailboxes, laddr));

	addr = vci_ascheme_build_addr(mgr->ascheme, mgr->slave_id, mgr->worker_id, laddr);

	mbox = malloc(sizeof(*mbox));
	if(!mbox)
		printfault(EXIT_NOMEM, "Out of memory: vci_create_ip");

	mbox->context_provider = cp;
	mbox->network = net;

	/* track it with a hashtable */
	hashtable_set(mgr->mailboxes, laddr, mbox);
	hashtable_set(mgr->networks_by_address, addr, net);

	return addr;
}

void vci_free_ip(vci_mgr_tp mgr, in_addr_t addr) {
	unsigned int laddr = vci_ascheme_get_node(mgr->ascheme, addr);
	vci_mailbox_tp mbox = hashtable_remove(mgr->mailboxes, laddr);
	if(mbox)
		vci_free_mailbox(mbox, laddr);
	return;
}

static void vci_free_modules(void* vmbox, int laddr) {
	vci_mailbox_tp mbox = vmbox;

	if(mbox) {
		/* call event */
		context_execute_destroy(mbox->context_provider);

		/* delete module memory */
		module_destroy_instance(mbox->context_provider->modinst);
	}
}

static void vci_free_mailbox(void * vmbox, int laddr) {
	vci_mailbox_tp mbox = vmbox;

	if(mbox) {
		/* delete vnetwork stack */
		vsocket_mgr_destroy(mbox->context_provider->vsocket_mgr);

		/* free the context provider data */
		if(global_sim_context.current_context == mbox->context_provider) {
			global_sim_context.current_context = NULL;
		}
		free(mbox->context_provider);
		mbox->context_provider = NULL;
		free(mbox);
		mbox = NULL;
	}

	return;
}

static vci_mailbox_tp vci_get_mailbox(vci_mgr_tp vci_mgr, in_addr_t ip) {
	if(vci_mgr != NULL) {
		return hashtable_get(vci_mgr->mailboxes, vci_ascheme_get_node(vci_mgr->ascheme, ip));
	}
	return NULL;
}

vci_mgr_tp vci_mgr_create (events_tp events, unsigned int slave_id, unsigned int worker_id, vci_addressing_scheme_tp scheme) {
	vci_mgr_tp rv = malloc(sizeof(*rv));

	rv->ascheme = scheme;
	rv->events = events;

	rv->mailboxes = hashtable_create(sysconfig_get_int("vci_mailbox_hashsize"), sysconfig_get_float("vci_mailbox_hashgrowth"));
	rv->networks_by_id =  hashtable_create(sysconfig_get_int("vci_network_hashsize"), sysconfig_get_float("vci_network_hashgrowth"));
	rv->networks_by_address = hashtable_create(sysconfig_get_int("vci_remote_node_netmap_hashsize"), sysconfig_get_float("vci_remote_node_netmap_hashgrowth"));

	rv->slave_id = slave_id;
	rv->worker_id = worker_id;

	rv->current_vsocket_mgr = NULL;

	return rv;
}

void vci_mgr_destroy(vci_mgr_tp mgr) {
	/* network cleanup */
	hashtable_walk(mgr->networks_by_id, &vci_free_network);
	hashtable_destroy(mgr->networks_by_id);
	hashtable_destroy(mgr->networks_by_address);

	/* first destroy all modules */
	hashtable_walk(mgr->mailboxes, &vci_free_modules);

	/* then the actual mailbox cleanup */
	hashtable_walk(mgr->mailboxes, &vci_free_mailbox);
	hashtable_destroy(mgr->mailboxes);

	free(mgr);
	mgr = NULL;
	return;
}

vci_network_tp vci_network_create(vci_mgr_tp mgr, unsigned int id) {
	vci_network_tp net = malloc(sizeof(vci_network_t));
	if(!net)
		printfault(EXIT_NOMEM, "vci_network_create: Out of memory");

	net->netid = id;

	hashtable_set(mgr->networks_by_id, id, net);

	return net;
}

static void vci_free_network(void * vnet, int netid) {
	vci_network_tp net = vnet;

	if(net) {
		free(net);
	}

	return;
}

void vci_track_network(vci_mgr_tp mgr, unsigned int network_id, in_addr_t addr) {
	/* we are being told that the remote node addr belongs to network_id */
	vci_network_tp net = hashtable_get(mgr->networks_by_address, addr);

	if(net != NULL) {
		dlogf(LOG_WARN, "vci_track_network: overwriting remote network mapping for %s\n", inet_ntoa_t(addr));
	} else {
		net = vci_network_create(mgr, network_id);
	}

	hashtable_set(mgr->networks_by_address, addr, net);
}

/**
 * Provides the underlying model for the network layer
 *
 * Based on the delay measurements in turbo-King
 * http://inl.info.ucl.ac.be/blogs/08-04-23-turbo-king-framework-large-scale-internet-delay-measurements
 * Paper:  http://irl.cs.tamu.edu/people/derek/papers/infocom2008.pdf
 * Note that we are looking mostly at link delay since we are modeling an inter AS delay
 *
 * We expect a CDF as follows:

   1|                         +++++++++++++++
    |                     +++
    |                  ++
    |                 +
    |                +
    |                +
    |                +
    |                +
    |                +
    |                +
    |               +
    |               +
    |              +
   0+++++++++++++++-----------------------------
    0                |
                Base Delay
                 |<----->|<----------|
                  Width      Tail
 */

// TODO check if we should do this in cdf_generate
//static unsigned int vci_model_delay(vci_netmodel_tp netmodel) {
//	if(netmodel != NULL) {
//		float flBase = 1.0f;
//		float flRandWidth = 0.0f;
//		int i = 0;
//
//		for(i=0;i<=VCI_NETMODEL_TIGHTNESS_FACTOR ;i++)
//		{
//			// Cummulatively computes random delay values
//			flBase = flBase * (1.0f - (dvn_rand_fast(RAND_MAX) / ((float)RAND_MAX/2)));
//		}
//
//		if(flBase < 0)
//			flRandWidth = flBase * netmodel->width;// Scales it to the desired width
//		else
//			flRandWidth = flBase * netmodel->tail_width;// Models the long tail
//
//		return (unsigned int) netmodel->base_delay + (unsigned int)flRandWidth;
//	} else {
//		return 0;
//	}
//}

static enum vci_location vci_get_relative_location(in_addr_t relative_to) {
	/* there are 3 cases - the caller and the given address are on:
	 * 	1. same machine(slave), same process(worker)
	 * 		(target_slave_id == mgr->slave_id && target_worker_id == mgr->worker_id)
	 * 	2. same machine(slave), different process(worker)
	 * 		(target_slave_id == mgr->slave_id && target_worker_id != mgr->worker_id)
	 * 	3. different machines(slaves)
	 * 		(target_slave_id != mgr->slave_id)
	 */

	if(relative_to == htonl(INADDR_LOOPBACK)) {
		return LOC_SAME_SLAVE_SAME_WORKER;
	}

	/* the mgr is at the caller's worker */
	sim_worker_tp worker = global_sim_context.sim_worker;
	if(worker == NULL) {
		return LOC_ERROR;
	}
	vci_mgr_tp vci_mgr = worker->vci_mgr;
	if(vci_mgr == NULL) {
		return LOC_ERROR;
	}

	unsigned int target_slave_id = vci_ascheme_get_slave(vci_mgr->ascheme, relative_to);
	unsigned int target_worker_id = vci_ascheme_get_worker(vci_mgr->ascheme, relative_to);

	if(target_slave_id == vci_mgr->slave_id && target_worker_id == vci_mgr->worker_id) {
		/* case 1 */
		return LOC_SAME_SLAVE_SAME_WORKER;
	}else {
		/* different worker, possibly remote. */
		if(target_slave_id == vci_mgr->slave_id){
			/* case 2 */
			return LOC_SAME_SLAVE_DIFFERENT_WORKER;
		} else {
			/* case 3 */
			return LOC_DIFFERENT_SLAVE_DIFFERENT_WORKER;
		}
	}
}

uint8_t vci_get_latency(in_addr_t src_addr, in_addr_t dst_addr,
		uint32_t* src_to_dst_lat, uint32_t* dst_to_src_lat) {
	int retval = 0;
	vci_scheduling_info_tp si = vci_get_scheduling_info(src_addr, dst_addr);
	if(si != NULL) {
		if(src_to_dst_lat != NULL) {
			*src_to_dst_lat = (uint32_t) simnet_graph_end2end_latency(si->worker->network_topology, si->src_net->netid, si->dst_net->netid);
		}
		if(dst_to_src_lat != NULL) {
			*dst_to_src_lat = (uint32_t) simnet_graph_end2end_latency(si->worker->network_topology, si->dst_net->netid, si->src_net->netid);
		}
		retval = 1;
	}

	if(si != NULL) {
		free(si);
	}
	return retval;
}

static vci_scheduling_info_tp vci_get_scheduling_info(in_addr_t src_addr, in_addr_t dst_addr) {
	sim_worker_tp worker = global_sim_context.sim_worker;
	if(worker == NULL) {
		dlogf(LOG_ERR, "vci_get_scheduling_info: error obtaining worker\n");
		return NULL;
	}

	vci_mgr_tp vci_mgr = worker->vci_mgr;
	if(vci_mgr == NULL) {
		dlogf(LOG_ERR, "vci_get_scheduling_info: error obtaining vci_mgr\n");
		return NULL;
	}

	vci_network_tp src_net = hashtable_get(vci_mgr->networks_by_address, src_addr);
	if(src_net == NULL) {
		dlogf(LOG_ERR, "vci_get_scheduling_info: error obtaining src network for %s\n", inet_ntoa_t(src_addr));
		return NULL;
	}

	vci_network_tp dst_net = hashtable_get(vci_mgr->networks_by_address, dst_addr);
	if(dst_net == NULL) {
		dlogf(LOG_ERR, "vci_get_scheduling_info: error obtaining dst network for %s\n", inet_ntoa_t(dst_addr));
		return NULL;
	}

	/* if we got here, all is good */
	vci_scheduling_info_tp sched = malloc(sizeof(vci_scheduling_info_t));
	sched->worker = worker;
	sched->vci_mgr = vci_mgr;
	sched->src_net = src_net;
	sched->dst_net = dst_net;
	return sched;
}

uint8_t vci_can_share_memory(in_addr_t node) {
	/* true if the caller can share memory with node */
	return vci_get_relative_location(node) == LOC_SAME_SLAVE_DIFFERENT_WORKER;
}

void vci_schedule_notify(in_addr_t addr, uint16_t sockd) {
	sim_worker_tp worker = global_sim_context.sim_worker;
	if(worker == NULL || worker->vci_mgr == NULL) {
		return;
	}

	/* this is always a local delivery, with a callback to the caller */

	vci_onnotify_tp notify_payload = malloc(sizeof(vci_onnotify_t));
	notify_payload->sockd = sockd;

	vci_event_tp vci_event = vci_create_event(worker->vci_mgr, VCI_EC_ONNOTIFY, worker->current_time + 1,
			addr, notify_payload);
	vci_schedule_event(worker->vci_mgr->events, vci_event);
}

void vci_schedule_poll(in_addr_t addr, vepoll_tp vep, uint32_t ms_delay) {
	sim_worker_tp worker = global_sim_context.sim_worker;
	if(worker == NULL || worker->vci_mgr == NULL) {
		return;
	}

	/* this is always a local delivery, with a callback to the caller */

	vci_onpoll_tp poll_payload = malloc(sizeof(vci_onpoll_t));
	poll_payload->vep = vep;

	vci_event_tp vci_event = vci_create_event(worker->vci_mgr, VCI_EC_ONPOLL, worker->current_time + ms_delay,
			addr, poll_payload);
	vci_schedule_event(worker->vci_mgr->events, vci_event);
}

void vci_schedule_dack(in_addr_t addr, uint16_t sockd, uint32_t ms_delay) {
	sim_worker_tp worker = global_sim_context.sim_worker;
	if(worker == NULL || worker->vci_mgr == NULL) {
		return;
	}

	/* this is always a local delivery, with a callback to the caller */

	vci_ondack_tp dack_payload = malloc(sizeof(vci_ondack_t));
	dack_payload->sockd = sockd;

	vci_event_tp vci_event = vci_create_event(worker->vci_mgr, VCI_EC_ONDACK, worker->current_time + ms_delay,
			addr, dack_payload);
	vci_schedule_event(worker->vci_mgr->events, vci_event);
}

static void vci_schedule_transferred(in_addr_t addr, uint32_t msdelay, enum vci_event_code code) {
	sim_worker_tp worker = global_sim_context.sim_worker;
	if(worker == NULL || worker->vci_mgr == NULL) {
		return;
	}

	/* this is always a local delivery, with a callback to the caller */

	vci_event_tp vci_event = vci_create_event(worker->vci_mgr, code, worker->current_time + msdelay,
			addr, NULL);
	vci_schedule_event(worker->vci_mgr->events, vci_event);
}

void vci_schedule_uploaded(in_addr_t addr, uint32_t msdelay) {
	vci_schedule_transferred(addr, msdelay, VCI_EC_ONUPLOADED);
}

void vci_schedule_downloaded(in_addr_t addr, uint32_t msdelay) {
	vci_schedule_transferred(addr, msdelay, VCI_EC_ONDOWNLOADED);
}

void vci_schedule_packet_loopback(rc_vpacket_pod_tp rc_packet, in_addr_t addr) {
	rc_vpacket_pod_retain_stack(rc_packet);

	vci_onpacket_tp packet_payload = malloc(sizeof(vci_onpacket_t));
	packet_payload->rc_pod = rc_packet;
	rc_vpacket_pod_retain(rc_packet);

	if(global_sim_context.sim_worker != NULL && global_sim_context.sim_worker->vci_mgr != NULL) {
		ptime_t deliver_time = global_sim_context.sim_worker->current_time + 1;

		vci_event_tp vci_event = vci_create_event(global_sim_context.sim_worker->vci_mgr, VCI_EC_ONPACKET, deliver_time,
				addr, packet_payload);
		vci_schedule_event(global_sim_context.sim_worker->vci_mgr->events, vci_event);
	}

	rc_vpacket_pod_release_stack(rc_packet);
}

void vci_schedule_packet(rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	vci_scheduling_info_tp si = NULL;
	int do_unlock = 1;

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet == NULL) {
		dlogf(LOG_ERR, "vci_schedule_packet: packet is NULL!\n");
		do_unlock = 0;
		goto ret;
	}
	si = vci_get_scheduling_info(packet->header.source_addr, packet->header.destination_addr);
	if(si == NULL) {
		dlogf(LOG_ERR, "vci_schedule_packet: scheduling information NULL!\n");
		goto ret;
	}

	in_addr_t src_addr = packet->header.source_addr;

	vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

	/* first thing to check is if network reliability forces us to 'drop'
	 * the packet. if so, get out of dodge doing as little as possible.
	 */
	if(dvn_rand_unit() > simnet_graph_end2end_reliablity(si->worker->network_topology, si->src_net->netid, si->dst_net->netid)){
		/* sender side is scheduling packets, but we are simulating
		 * the packet being dropped between sender and receiver, so
		 * it will need to be retransmitted */
		vci_schedule_retransmit(rc_packet, src_addr);
		goto ret;
	}

	/* packet will make it through, find latency */
	unsigned int latency = (unsigned int) simnet_graph_end2end_latency(si->worker->network_topology, si->src_net->netid, si->dst_net->netid);
	ptime_t deliver_time = si->worker->current_time + latency;

	packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET | LC_TARGET_PAYLOAD);

	/* where is the packet going */
	enum vci_location loc = vci_get_relative_location(packet->header.destination_addr);

	switch (loc) {

		/* delivery locally to another node on the same worker. */
		case LOC_SAME_SLAVE_SAME_WORKER: {
			vci_onpacket_tp packet_payload = malloc(sizeof(vci_onpacket_t));
			packet_payload->rc_pod = rc_packet;
			rc_vpacket_pod_retain(rc_packet);

			vci_event_tp vci_event = vci_create_event(si->vci_mgr, VCI_EC_ONPACKET, deliver_time,
					packet->header.destination_addr, packet_payload);
			vci_schedule_event(si->vci_mgr->events, vci_event);
			break;
		}

		/* delivery locally to another node on a different worker. */
		case LOC_SAME_SLAVE_DIFFERENT_WORKER: {
			int frametype = 0;
			nbdf_tp frame = NULL;

			/* either the packet exists in a shmcabinet, or we send it through a pipecloud */
			if(sysconfig_get_int("vnetwork_use_shmcabinet")) {

				/* make sure we can get our shm information */
				if(rc_packet->pod == NULL || rc_packet->pod->shmitem_packet == NULL ||
						rc_packet->pod->shmitem_packet->shm == NULL) {
					dlogf(LOG_ERR, "vci_schedule_packet: error scheduling packet, problem getting packet shm id information\n");
					goto ret;
				}

				shmcabinet_info_tp shminfo_packet = &(rc_packet->pod->shmitem_packet->shm->info);

				/* check for payload */
				if(packet->data_size > 0) {
					/* payload present */
					frametype = SIM_FRAME_VCI_PACKET_PAYLOAD_SHMCABINET;

					/* make sure we can get our shm information */
					if(rc_packet->pod->shmitem_payload == NULL ||
							rc_packet->pod->shmitem_payload->shm == NULL) {
						dlogf(LOG_ERR, "vci_schedule_packet: error scheduling packet, problem getting payload shm id information\n");
						goto ret;
					}

					shmcabinet_info_tp shminfo_payload = &(rc_packet->pod->shmitem_payload->shm->info);

					/* send shm info for the packet and payload */
					frame = nbdf_construct("taiiiiiiii",
							deliver_time, packet->header.destination_addr,
							shminfo_packet->process_id, shminfo_packet->cabinet_id, shminfo_packet->cabinet_size,
							rc_packet->pod->shmitem_packet->slot_id,
							shminfo_payload->process_id, shminfo_payload->cabinet_id, shminfo_payload->cabinet_size,
							rc_packet->pod->shmitem_payload->slot_id);
				} else {
					/* no payload */
					frametype = SIM_FRAME_VCI_PACKET_NOPAYLOAD_SHMCABINET;

					/* send shm info for the packet only */
					frame = nbdf_construct("taiiii",
							deliver_time, packet->header.destination_addr,
							shminfo_packet->process_id, shminfo_packet->cabinet_id, shminfo_packet->cabinet_size,
							rc_packet->pod->shmitem_packet->slot_id);
				}
			} else {
				if(packet->data_size > 0) {
					frametype = SIM_FRAME_VCI_PACKET_PAYLOAD;
				} else {
					frametype = SIM_FRAME_VCI_PACKET_NOPAYLOAD;
				}

				/* send everything in the packet so it can be reconstructed by the other node */
				frame = vci_construct_pipecloud_packet_frame(deliver_time, packet);
			}

			unsigned int target_worker_id = vci_ascheme_get_worker(si->vci_mgr->ascheme, packet->header.destination_addr);
			dvn_packet_route(DVNPACKET_WORKER, DVNPACKET_LAYER_SIM, target_worker_id, frametype, frame);

			nbdf_free(frame);
			break;
		}

		/* delivery remotely to another node on a different worker. */
		case LOC_DIFFERENT_SLAVE_DIFFERENT_WORKER: {
			int frametype = 0;

			if(packet->data_size > 0) {
				frametype = SIM_FRAME_VCI_PACKET_PAYLOAD;
			} else {
				frametype = SIM_FRAME_VCI_PACKET_NOPAYLOAD;
			}

			/* send all packet contents across the real network */
			nbdf_tp frame = vci_construct_pipecloud_packet_frame(deliver_time, packet);

			unsigned int target_worker_id = vci_ascheme_get_worker(si->vci_mgr->ascheme, packet->header.destination_addr);
			dvn_packet_route(DVNPACKET_SLAVE, DVNPACKET_LAYER_SIM, target_worker_id, frametype, frame);

			nbdf_free(frame);
			break;
		}

		default: {
			dlogf(LOG_ERR, "vci_schedule_packet: error determining node location\n");
			break;
		}
	}

ret:
	free(si);
	if(do_unlock) {
		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET | LC_TARGET_PAYLOAD);
	}
	rc_vpacket_pod_release_stack(rc_packet);
}

static nbdf_tp vci_construct_pipecloud_packet_frame(ptime_t time, vpacket_tp packet) {
	nbdf_tp frame = NULL;

	if(packet != NULL) {
		if(packet->data_size > 0) {
			frame = nbdf_construct("tcapapiiicb",
					time, packet->header.protocol,
					packet->header.source_addr, packet->header.source_port,
					packet->header.destination_addr, packet->header.destination_port,
					packet->tcp_header.sequence_number, packet->tcp_header.acknowledgement,
					packet->tcp_header.advertised_window, packet->tcp_header.flags,
					(unsigned int)packet->data_size, packet->payload);
		} else {
			frame = nbdf_construct("tcapapiiic",
					time, packet->header.protocol,
					packet->header.source_addr, packet->header.source_port,
					packet->header.destination_addr, packet->header.destination_port,
					packet->tcp_header.sequence_number, packet->tcp_header.acknowledgement,
					packet->tcp_header.advertised_window, packet->tcp_header.flags);
		}

	}

	return frame;
}

void vci_schedule_retransmit(rc_vpacket_pod_tp rc_packet, in_addr_t caller_addr) {
	/* TODO refactor - this was hacked to allow loopback addresses */
	rc_vpacket_pod_retain_stack(rc_packet);
	vci_scheduling_info_tp si = NULL;

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet == NULL) {
		goto ret2;
	}

	vci_mgr_tp vci_mgr = NULL;
	ptime_t deliver_time;
	if(packet->header.source_addr == htonl(INADDR_LOOPBACK) && global_sim_context.sim_worker != NULL) {
		/* going to loopback, virtually no delay */
		vci_mgr = global_sim_context.sim_worker->vci_mgr;
		deliver_time = global_sim_context.sim_worker->current_time + 1;
	} else {
		si = vci_get_scheduling_info(packet->header.source_addr, packet->header.destination_addr);
		if(si == NULL) {
			goto ret;
		}

		/* source should retransmit.
		 * retransmit timers depend on RTT, use latency as approximation since in most
		 * cases the dest will be dropping a packet and one latency has already been incurred. */
		unsigned int latency = (unsigned int) simnet_graph_end2end_latency(si->worker->network_topology, si->src_net->netid, si->dst_net->netid);
		deliver_time = si->worker->current_time + latency;

		vci_mgr = si->vci_mgr;
	}

	/* find source relative to caller so we know how to send event */
	enum vci_location loc = vci_get_relative_location(packet->header.source_addr);

	switch (loc) {
		case LOC_SAME_SLAVE_SAME_WORKER: {
			vci_onretransmit_tp retransmit_payload = malloc(sizeof(vci_onretransmit_t));
			retransmit_payload->src_port = packet->header.source_port;
			retransmit_payload->dst_addr = packet->header.destination_addr;
			retransmit_payload->dst_port = packet->header.destination_port;
			retransmit_payload->retransmit_key = packet->tcp_header.sequence_number;

			/* deliver to src_addr, the other end of the conenction. if that is 127.0.0.1,
			 * then use caller addr so vci can do the node lookup.
			 */
			in_addr_t deliver_to = 0;
			if(packet->header.source_addr == htonl(INADDR_LOOPBACK)) {
				deliver_to = caller_addr;
			} else {
				deliver_to = packet->header.source_addr;
			}

			/* event tracker will hold on to the packet */
			rc_vpacket_pod_retain(rc_packet);

			vci_event_tp vci_event = vci_create_event(vci_mgr, VCI_EC_ONRETRANSMIT, deliver_time,
					deliver_to, retransmit_payload);
			vci_schedule_event(vci_mgr->events, vci_event);
			break;
		}

		case LOC_SAME_SLAVE_DIFFERENT_WORKER:
		case LOC_DIFFERENT_SLAVE_DIFFERENT_WORKER: {
			int frametype = SIM_FRAME_VCI_RETRANSMIT;
			unsigned char route_type = loc == LOC_SAME_SLAVE_DIFFERENT_WORKER ?
					DVNPACKET_WORKER : DVNPACKET_SLAVE;

			nbdf_tp frame = nbdf_construct("tapapi",
					deliver_time,
					packet->header.source_addr,
					packet->header.source_port,
					packet->header.destination_addr,
					packet->header.destination_port,
					packet->tcp_header.sequence_number);

			unsigned int target_worker_id = vci_ascheme_get_worker(si->vci_mgr->ascheme, packet->header.source_addr);
			dvn_packet_route(route_type, DVNPACKET_LAYER_SIM, target_worker_id, frametype, frame);

			nbdf_free(frame);
			break;
		}

		default: {
			dlogf(LOG_ERR, "vci_schedule_retransmit: error determining node location\n");
			break;
		}
	}

ret:
	if(si != NULL) {
		free(si);
	}
	vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
ret2:
	rc_vpacket_pod_release_stack(rc_packet);
}

void vci_schedule_close(in_addr_t caller_addr, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, uint32_t rcv_end) {
	if(global_sim_context.sim_worker != NULL &&
			global_sim_context.sim_worker->destroying) {
		/* then we dont care */
		return;
	}

	/* TODO refactor - this was hacked to allow loopback addresses */
	vci_mgr_tp vci_mgr = NULL;
	ptime_t deliver_time = 0;
	vci_scheduling_info_tp si = NULL;

	if(src_addr == htonl(INADDR_LOOPBACK) || dst_addr == htonl(INADDR_LOOPBACK)) {
		/* going to loopback, virtually no delay */
		vci_mgr = global_sim_context.sim_worker->vci_mgr;
		deliver_time = global_sim_context.sim_worker->current_time + 1;
	} else {
		si = vci_get_scheduling_info(src_addr, dst_addr);
		if(si == NULL) {
			return;
		}
		unsigned int latency = (unsigned int) simnet_graph_end2end_latency(si->worker->network_topology, si->src_net->netid, si->dst_net->netid);
		deliver_time = si->worker->current_time + latency;
		vci_mgr = si->vci_mgr;
	}


	enum vci_location loc = vci_get_relative_location(dst_addr);

	switch (loc) {
		case LOC_SAME_SLAVE_SAME_WORKER: {
			vci_onclose_tp close_payload = malloc(sizeof(vci_onclose_t));
			close_payload->dst_port = dst_port;
			close_payload->src_addr = src_addr;
			close_payload->src_port = src_port;
			close_payload->rcv_end = rcv_end;

			/* deliver to dst_addr, the other end of the conenction. if that is 127.0.0.1,
			 * then use caller addr so vsi can do the node lookup.
			 */
			in_addr_t deliver_to = 0;
			if(dst_addr == htonl(INADDR_LOOPBACK)) {
				deliver_to = caller_addr;
			} else {
				deliver_to = dst_addr;
			}

			vci_event_tp vci_event = vci_create_event(vci_mgr, VCI_EC_ONCLOSE, deliver_time, deliver_to, close_payload);
			vci_schedule_event(vci_mgr->events, vci_event);
			break;
		}

		case LOC_SAME_SLAVE_DIFFERENT_WORKER:
		case LOC_DIFFERENT_SLAVE_DIFFERENT_WORKER: {
			int frametype = SIM_FRAME_VCI_CLOSE;
			unsigned char route_type = loc == LOC_SAME_SLAVE_DIFFERENT_WORKER ?
					DVNPACKET_WORKER : DVNPACKET_SLAVE;

			nbdf_tp frame = nbdf_construct("tapapi", deliver_time,
					dst_addr, dst_port, src_addr, src_port, rcv_end);

			unsigned int target_worker_id = vci_ascheme_get_worker(si->vci_mgr->ascheme, dst_addr);
			dvn_packet_route(route_type, DVNPACKET_LAYER_SIM, target_worker_id, frametype, frame);

			nbdf_free(frame);
			break;
		}

		default: {
			dlogf(LOG_ERR, "vci_schedule_close: error determining node location\n");
			break;
		}
	}

	free(si);
}

static vci_event_tp vci_create_event(vci_mgr_tp vci_mgr, enum vci_event_code code, ptime_t deliver_time,
		in_addr_t node_addr, void* payload) {
	vci_event_tp vci_event = malloc(sizeof(vci_event_t));
	vci_event->code = code;
	vci_event->deliver_time = deliver_time;
	vci_event->node_addr = node_addr;
	vci_event->payload = payload;

	vsocket_mgr_tp vs = vci_mgr->current_vsocket_mgr;
	if(vs == NULL) {
		vs = global_sim_context.current_context->vsocket_mgr;
	}
	vci_event->owner_addr = vs->addr;
	vci_event->cpu_delay_position = vcpu_get_delay(vs->vcpu);
	return vci_event;
}

void vci_destroy_event(vci_event_tp vci_event) {
	if(vci_event == NULL) {
		return;
	}

	switch (vci_event->code) {

		/* special payloads containing pointers */
		case VCI_EC_ONPACKET: {
			vci_onpacket_tp packet_payload = vci_event->payload;
			if(packet_payload != NULL) {
				rc_vpacket_pod_release(packet_payload->rc_pod);
			}
			free(packet_payload);
			break;
		}

		/* simple payloads with no pointers */
		case VCI_EC_ONNOTIFY:
		case VCI_EC_ONPOLL:
		case VCI_EC_ONDACK:
		case VCI_EC_ONRETRANSMIT:
		case VCI_EC_ONCLOSE:
		default: {
			free(vci_event->payload);
			break;
		}

		/* no payload */
		case VCI_EC_ONDOWNLOADED:
		case VCI_EC_ONUPLOADED: {
			break;
		}
	}

	free(vci_event);
}

static void vci_schedule_event(events_tp events, vci_event_tp vci_event) {
	if(vci_event->node_addr == htonl(INADDR_LOOPBACK) ||
			vci_event->node_addr == htonl(INADDR_NONE)) {
		dlogf(LOG_WARN, "vci_schedule_event: scheduling event with address %s\n", inet_ntoa_t(vci_event->node_addr));
	}
	events_schedule(events, vci_event->deliver_time, vci_event, EVENTS_TYPE_VCI);
}

static vsocket_mgr_tp vci_enter_vnetwork_context(vci_mgr_tp vci_mgr, in_addr_t addr) {
	vci_mailbox_tp mbox = vci_get_mailbox(vci_mgr, addr);

	if(mbox == NULL || mbox->context_provider == NULL ||
			mbox->context_provider->vsocket_mgr == NULL) {
		dlogf(LOG_ERR, "vci_enter_vnetwork_context: NULL pointer when entering vnetwork context for %s\n", inet_ntoa_t(addr));
		return NULL;
	}

	vci_mgr->current_vsocket_mgr = mbox->context_provider->vsocket_mgr;
	return vci_mgr->current_vsocket_mgr;
}

static void vci_exit_vnetwork_context(vci_mgr_tp vci_mgr) {
	vci_mgr->current_vsocket_mgr = NULL;
}

void vci_exec_event (vci_mgr_tp vci_mgr, vci_event_tp vci_event) {
	if(vci_event == NULL) {
		return;
	}

	vsocket_mgr_tp vs_mgr = vci_enter_vnetwork_context(vci_mgr, vci_event->node_addr);

	if(vs_mgr != NULL) {
		if(vci_event->owner_addr != vs_mgr->addr) {
			/* i didnt create the event. the delay attached is someone elses.
			 * this is the first i've seen of this event. take ownership and
			 * update the cpu delay to mine. */
			vci_event->owner_addr = vs_mgr->addr;
			vci_event->cpu_delay_position = vcpu_get_delay(vs_mgr->vcpu);
		}

		/* set our current position so any calls to read/write knows how
		 * much delay we've already absorbed.
		 */
		vcpu_set_absorbed(vs_mgr->vcpu, vci_event->cpu_delay_position);

		/* check if we are allowed to execute or have to wait for cpu delays */
		if(vcpu_is_blocking(vs_mgr->vcpu)) {
			/* this event is delayed due to cpu, so reschedule it */
			uint64_t current_delay = vcpu_get_delay(vs_mgr->vcpu);

			if(vci_event->cpu_delay_position > current_delay) {
				/* impossible for our cpu to lose delay */
				dlogf(LOG_ERR, "vci_exec_event: delay on event (%lu) is greater than our CPU delay (%lu). Killing it. Things probably wont work right.\n", vci_event->cpu_delay_position, current_delay);
				goto ret;
			}

			uint64_t nanos_offset = current_delay - vci_event->cpu_delay_position;
			uint64_t millis_offset = (uint64_t)(nanos_offset / ((uint64_t)1000000));

			if(millis_offset > 0) {
				vci_event->cpu_delay_position += ((uint64_t)(millis_offset * 1000000));
				vci_event->deliver_time += millis_offset;
				vci_schedule_event(vci_mgr->events, vci_event);
				debugf("vci_exec_event: event blocked on CPU, rescheduled for %lu ms from now\n", millis_offset);
				goto exit;
			}

		}
	}

	vci_mailbox_tp mbox = vci_get_mailbox(vci_mgr, vci_event->node_addr);
	if(mbox == NULL || mbox->context_provider == NULL ||
			vs_mgr == NULL) {
		goto ret;
	}

	switch (vci_event->code) {

		case VCI_EC_ONPACKET: {
			vci_onpacket_tp packet_payload = vci_event->payload;
			if(packet_payload != NULL) {
				vpacket_log_debug(packet_payload->rc_pod);
				vtransport_mgr_onpacket(vs_mgr->vt_mgr, packet_payload->rc_pod);
			}
			break;
		}

		case VCI_EC_ONNOTIFY: {
			vci_onnotify_tp notify_payload = vci_event->payload;
			if(notify_payload != NULL) {
				vsocket_mgr_onnotify(vs_mgr, mbox->context_provider, notify_payload->sockd);
			}
			break;
		}

		case VCI_EC_ONPOLL: {
			vci_onpoll_tp poll_payload = vci_event->payload;
			if(poll_payload != NULL) {
				vepoll_onpoll(poll_payload->vep);
			}
			break;
		}

		case VCI_EC_ONDACK: {
			vci_ondack_tp dack_payload = vci_event->payload;
			if(dack_payload != NULL) {
				vtcp_ondack(vs_mgr, dack_payload->sockd);
			}
			break;
		}

		case VCI_EC_ONDOWNLOADED: {
			/* no payload */
			vtransport_mgr_ondownloaded(vs_mgr->vt_mgr);
			break;
		}

		case VCI_EC_ONUPLOADED: {
			/* no payload */
			vtransport_mgr_onuploaded(vs_mgr->vt_mgr);
			break;
		}

		case VCI_EC_ONRETRANSMIT: {
			vci_onretransmit_tp retransmit_payload = vci_event->payload;
			vtransport_onretransmit(vs_mgr,
					retransmit_payload->dst_addr, retransmit_payload->dst_port,
					retransmit_payload->src_port, retransmit_payload->retransmit_key);
			break;
		}

		case VCI_EC_ONCLOSE: {
			vci_onclose_tp close_payload = vci_event->payload;
			vtransport_onclose(vs_mgr,
					close_payload->src_addr, close_payload->src_port,
					vci_event->node_addr, close_payload->dst_port, close_payload->rcv_end);
			break;
		}

		default: {
			break;
		}
	}

ret:
	vci_destroy_event(vci_event);
exit:
	vci_exit_vnetwork_context(vci_mgr);
}

void vci_deposit(vci_mgr_tp vci_mgr, nbdf_tp frame, int frametype) {
	/* we have an incoming frame that contains an event from another worker */
	vci_event_tp vci_event = vci_decode(vci_mgr, frame, frametype);
	if(vci_event == NULL) {
		return;
	}

	/* make sure this event is actually meant for us */
	unsigned int target_slave_id = vci_ascheme_get_slave(vci_mgr->ascheme, vci_event->node_addr);
	unsigned int target_worker_id = vci_ascheme_get_worker(vci_mgr->ascheme, vci_event->node_addr);

	if(target_slave_id != vci_mgr->slave_id || target_worker_id != vci_mgr->worker_id) {
		vci_destroy_event(vci_event);
		return;
	}

	/* schedule the event */
	switch (vci_event->code) {
		case VCI_EC_ONPACKET:
		case VCI_EC_ONRETRANSMIT:
		case VCI_EC_ONCLOSE: {
			vci_schedule_event(vci_mgr->events, vci_event);
			break;
		}

		case VCI_EC_ONNOTIFY:
		case VCI_EC_ONDOWNLOADED:
		case VCI_EC_ONUPLOADED:
		case VCI_EC_ONDACK:
		default: {
			dlogf(LOG_ERR, "vci_deposit: received network frame containing an event that should be scheduled locally\n");
			vci_destroy_event(vci_event);
			break;
		}
	}
}

static vci_event_tp vci_decode(vci_mgr_tp vci_mgr, nbdf_tp frame, int frametype) {
	vci_event_tp vci_event = NULL;

	/* if we are getting a frame, it must have come from another process.
	 *
	 * if SIM_FRAME_VCI_LOCAL_PACKET, it came from the same machine. in this
	 * case the frame may either contain shm connection information (if config
	 * option vnetwork_use_shmcabinet == 1) otherwise it contains the entire
	 * packet.
	 *
	 * if SIM_FRAME_VCI_REMOTE_PACKET, it came from another machine and contains
	 * the entire packet (shm is not an option)
	 *
	 * other frame types do not use shm, they send all event info in the frame
	 */

	switch (frametype) {

		case SIM_FRAME_VCI_PACKET_NOPAYLOAD:
		case SIM_FRAME_VCI_PACKET_PAYLOAD: {
			ptime_t time = 0;
			in_addr_t addr = 0;
			vci_onpacket_tp payload = NULL;

			/* reconstruct entire packet from pipecloud frame */
			rc_vpacket_pod_tp rc_pod = vpacket_mgr_empty_packet_create();

			if(rc_pod == NULL || rc_pod->pod == NULL || rc_pod->pod->vpacket == NULL) {
				rc_vpacket_pod_release(rc_pod);
				break;
			}

			vpacket_tp packet = rc_pod->pod->vpacket;

			if(frametype == SIM_FRAME_VCI_PACKET_PAYLOAD) {
				/* our packet has a payload */
				unsigned int data_size = 0;
				char flags;

				nbdf_read(frame, "tcapapiiicB",
						&time, &(packet->header.protocol),
						&(packet->header.source_addr), &(packet->header.source_port),
						&(packet->header.destination_addr), &(packet->header.destination_port),
						&(packet->tcp_header.sequence_number), &(packet->tcp_header.acknowledgement),
						&(packet->tcp_header.advertised_window), &flags,
						&data_size, &(packet->payload));

				/* nbdf_read would not properly fill the bits of these vars, make sure
				 * they are casted correctly */
				packet->tcp_header.flags = (enum vpacket_tcp_flags) flags;
				packet->data_size = (uint16_t) data_size;
				addr = packet->header.destination_addr;
			} else { /* SIM_FRAME_VCI_PACKET_NOPAYLOAD */
				/* no payload */
				char flags;

				nbdf_read(frame, "tcapapiiic",
						&time, &(packet->header.protocol),
						&(packet->header.source_addr), &(packet->header.source_port),
						&(packet->header.destination_addr), &(packet->header.destination_port),
						&(packet->tcp_header.sequence_number), &(packet->tcp_header.acknowledgement),
						&(packet->tcp_header.advertised_window), &flags);

				/* nbdf_read would not properly fill the bits of these vars, make sure
				 * they are casted correctly */
				packet->tcp_header.flags = (enum vpacket_tcp_flags) flags;
				packet->data_size = 0;
				packet->payload = NULL;
				addr = packet->header.destination_addr;
			}

			/* now we know the dest addr */
			vsocket_mgr_tp vs_mgr = vci_enter_vnetwork_context(vci_mgr, addr);
			if(vs_mgr == NULL) {
				rc_vpacket_pod_release(rc_pod);
				break;
			} else {
				rc_pod->pod->vp_mgr = vs_mgr->vp_mgr;
			}

			vpacket_mgr_setup_locks(rc_pod->pod);

			payload = malloc(sizeof(vci_onpacket_t));
			payload->rc_pod = rc_pod;

			vci_event = vci_create_event(vci_mgr, VCI_EC_ONPACKET, time, addr, payload);

			vci_exit_vnetwork_context(vci_mgr);
			break;
		}

		case SIM_FRAME_VCI_PACKET_NOPAYLOAD_SHMCABINET:
		case SIM_FRAME_VCI_PACKET_PAYLOAD_SHMCABINET: {
			ptime_t time = 0;
			in_addr_t addr = 0;

			/* get the shm info from the frame */
			shmcabinet_info_t shminfo_packet;
			uint32_t slot_id_packet;

			/* TODO nbdf doesnt directly support size_t, so we use uint32 for size_t here.
			 * we have to initialize to zero in case our read doesnt fill the size_t. */
			shminfo_packet.cabinet_size = 0;

			vci_onpacket_tp payload = malloc(sizeof(vci_onpacket_t));

			if(frametype == SIM_FRAME_VCI_PACKET_PAYLOAD_SHMCABINET) {
				/* we also need shm for payload */
				shmcabinet_info_t shminfo_payload;
				uint32_t slot_id_payload;
				shminfo_payload.cabinet_size = 0;

				nbdf_read(frame, "taiiiiiiii", &time, &addr,
						&shminfo_packet.process_id, &shminfo_packet.cabinet_id,
						&shminfo_packet.cabinet_size, &slot_id_packet,
						&shminfo_payload.process_id, &shminfo_payload.cabinet_id,
						&shminfo_payload.cabinet_size, &slot_id_payload);

				vsocket_mgr_tp vs_mgr = vci_enter_vnetwork_context(vci_mgr, addr);
				if(vs_mgr == NULL) {
					free(payload);
					break;
				}

				payload->rc_pod = vpacket_mgr_attach_shared_packet(vs_mgr->vp_mgr,
						&shminfo_packet, slot_id_packet, &shminfo_payload, slot_id_payload);

			} else { /* SIM_FRAME_VCI_PACKET_NOPAYLOAD_SHMCABINET */
				/* no payload to read */
				nbdf_read(frame, "taiiii", &time, &addr,
						&shminfo_packet.process_id, &shminfo_packet.cabinet_id,
						&shminfo_packet.cabinet_size, &slot_id_packet);

				vsocket_mgr_tp vs_mgr = vci_enter_vnetwork_context(vci_mgr, addr);
				if(vs_mgr == NULL) {
					free(payload);
					break;
				}

				payload->rc_pod = vpacket_mgr_attach_shared_packet(vs_mgr->vp_mgr,
						&shminfo_packet, slot_id_packet, NULL, 0);
			}

			vci_event = vci_create_event(vci_mgr, VCI_EC_ONPACKET, time, addr, payload);

			vci_exit_vnetwork_context(vci_mgr);
			break;
		}

		case SIM_FRAME_VCI_RETRANSMIT: {
			ptime_t time = 0;
			in_addr_t addr = 0;

			vci_onretransmit_tp payload = malloc(sizeof(vci_onretransmit_t));
			nbdf_read(frame, "tapapi", &time, &addr,
					&payload->src_port, &payload->dst_addr, &payload->dst_port,
					&payload->retransmit_key);

			vci_event = vci_create_event(vci_mgr, VCI_EC_ONRETRANSMIT, time, addr, payload);
			break;
		}

		case SIM_FRAME_VCI_CLOSE: {
			ptime_t time = 0;
			in_addr_t addr = 0;

			vci_onclose_tp payload = malloc(sizeof(vci_onclose_t));
			nbdf_read(frame, "tapapi", &time, &addr,
					&payload->dst_port, &payload->src_addr, &payload->src_port,
					&payload->rcv_end);

			vci_event = vci_create_event(vci_mgr, VCI_EC_ONCLOSE, time, addr, payload);
			break;
		}

		default: {
			dlogf(LOG_WARN, "vci_decode: unrecognized frame type %i\n", frametype);
			break;
		}
	}

	return vci_event;
}

#if 0
/* used for testing encoding and decoding packets */
static void quickprint(vpacket_tp vpacket) {
	if(vpacket != NULL) {
		char srcip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &vpacket->header.source_addr, srcip, sizeof(srcip));
		char dstip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &vpacket->header.destination_addr, dstip, sizeof(dstip));

		if(vpacket->header.protocol == SOCK_STREAM) {

			debugf("vpacket_log: TCP from %s:%u to %s:%u %u seq#:%u ack#:%u win#:%u bytes:%u\n",
					srcip,
					vpacket->header.source_port,
					dstip,
					vpacket->header.destination_port,
					vpacket->header.protocol,
					vpacket->tcp_header.sequence_number,
					vpacket->tcp_header.acknowledgement,
					vpacket->tcp_header.advertised_window,
					vpacket->data_size);
		}
	}
}

static void quicktest() {
	vpacket_t p;
	memcpy(p.payload, "THIS_IS_DATA", 12);
	p.data_size = 12;
	p.header.source_addr = 1;
	p.header.source_port = 2;
	p.header.destination_addr = 3;
	p.header.destination_port = 4;
	p.header.protocol = SOCK_STREAM;
	p.tcp_header.acknowledgement = 5;
	p.tcp_header.sequence_number = 6;
	p.tcp_header.advertised_window = 7;
	p.tcp_header.flags = 8;

	nbdf_tp frame = vci_construct_pipecloud_packet_frame(12345678, &p);

	vpacket_t pin;
	ptime_t t;
	in_addr_t a;

	vci_decode_pipecloud_packet_frame(frame, &t, &a, &pin);

	quickprint(&p);
	quickprint(&pin);
}
#endif
