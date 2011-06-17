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

#include <stdlib.h>
#include <stdint.h>

#include "log.h"
#include "vtransport_mgr.h"
#include "vtransport.h"
#include "vtransport_processing.h"
#include "vbuffer.h"
#include "vpacket_mgr.h"
#include "vpacket.h"
#include "vci.h"
#include "sim.h"

static vtransport_mgr_inq_tp vtransport_mgr_create_buffer(uint64_t max_size);
static void vtransport_mgr_destroy_buffer(vtransport_mgr_inq_tp buffer);
static uint8_t vtransport_mgr_is_acceptable_in(vtransport_mgr_tp vt_mgr, uint16_t data_size);

vtransport_mgr_tp vtransport_mgr_create(vsocket_mgr_tp vsocket_mgr, uint32_t KBps_down, uint32_t KBps_up){
	vtransport_mgr_tp vt_mgr = malloc(sizeof(vtransport_mgr_t));

	vt_mgr->vsocket_mgr = vsocket_mgr;

	vt_mgr->KBps_down = KBps_down;
	vt_mgr->KBps_up = KBps_up;

	uint64_t Bps_down = KBps_down * 1024;
	uint64_t Bps_up = KBps_up * 1024;
	vt_mgr->nanos_per_byte_down = 1000000000.0 / Bps_down;
	vt_mgr->nanos_per_byte_up = 1000000000.0 / Bps_up;

	vt_mgr->ready_to_send = list_create();
	vt_mgr->ok_to_fire_send = 1;

	/* TODO: make this config option?
	 * burst size - packets on the wire waiting to be received */
	vt_mgr->inq = vtransport_mgr_create_buffer(Bps_down);
	vt_mgr->ok_to_fire_recv = 1;

	vt_mgr->last_time_sent = 0;
	vt_mgr->last_time_recv = 0;
	vt_mgr->nanos_consumed_sent = 0;
	vt_mgr->nanos_consumed_recv = 0;

	return vt_mgr;
}

void vtransport_mgr_destroy(vtransport_mgr_tp vt_mgr) {
	if(vt_mgr != NULL) {
		vtransport_mgr_destroy_buffer(vt_mgr->inq);

		/* we are not responsible for the transports, so just delete list */
		while(list_get_size(vt_mgr->ready_to_send) > 0) {
			void* malloced_sockd = list_pop_back(vt_mgr->ready_to_send);
			if(malloced_sockd != NULL) {
				free(malloced_sockd);
			}
		}
		list_destroy(vt_mgr->ready_to_send);
		vt_mgr->ready_to_send = NULL;

		vt_mgr->ok_to_fire_send = 0;
		vt_mgr->ok_to_fire_recv = 0;
		vt_mgr->nanos_per_byte_down = 0.0;
		vt_mgr->nanos_per_byte_up = 0.0;
		vt_mgr->vsocket_mgr = NULL;

		free(vt_mgr);
	}
}

static vtransport_mgr_inq_tp vtransport_mgr_create_buffer(uint64_t max_size) {
	vtransport_mgr_inq_tp buffer = malloc(sizeof(vtransport_mgr_inq_t));
	buffer->buffer = list_create();
	buffer->max_size = max_size;
	buffer->current_size = 0;
	return buffer;
}

static void vtransport_mgr_destroy_buffer(vtransport_mgr_inq_tp buffer) {
	if(buffer == NULL || buffer->buffer == NULL) {
		return;
	}

	while(list_get_size(buffer->buffer) > 0) {
		vtransport_item_tp titem = list_pop_back(buffer->buffer);
		if(titem != NULL) {
			rc_vpacket_pod_release(titem->rc_packet);
		}
	}
	list_destroy(buffer->buffer);
	buffer->buffer = NULL;

	buffer->max_size = 0;
	buffer->current_size = 0;
	free(buffer);
}

static uint8_t vtransport_mgr_is_acceptable_in(vtransport_mgr_tp vt_mgr, uint16_t data_size) {
	if(vt_mgr != NULL && vt_mgr->inq != NULL) {
		if(data_size <= (vt_mgr->inq->max_size - vt_mgr->inq->current_size)) {
			return 1;
		}
	}
	return 0;
}

void vtransport_mgr_ready_receive(vtransport_mgr_tp vt_mgr, vsocket_tp sock, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);

	if(vt_mgr == NULL || vt_mgr->inq == NULL ||
			vt_mgr->inq->buffer == NULL || rc_packet == NULL) {
		goto ret;
	}

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet != NULL) {
		if(vtransport_mgr_is_acceptable_in(vt_mgr, packet->data_size)) {
			vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
			/* accept the packet in our incoming queue */
			vtransport_item_tp titem = vtransport_create_item(sock->sock_desc, rc_packet);

			list_push_back(vt_mgr->inq->buffer, titem);

			vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
			vt_mgr->inq->current_size += packet->data_size;
			vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

			/* trigger recv event if necessary */
			if(vt_mgr->ok_to_fire_recv) {
				vtransport_mgr_download_next(vt_mgr);
			}
		} else {
			vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
			debugf("vtransport_mgr_ready_receive: no space to receive packet, dropping\n");
			if(sock->type == SOCK_STREAM) {
				vci_schedule_retransmit(rc_packet, vt_mgr->vsocket_mgr->addr);
			}
		}
	} else {
		dlogf(LOG_ERR, "vtransport_mgr_ready_receive: incoming packet is NULL!\n");
	}

ret:
	rc_vpacket_pod_release_stack(rc_packet);
}

void vtransport_mgr_download_next(vtransport_mgr_tp vt_mgr) {
	if(vt_mgr == NULL || vt_mgr->inq == NULL) {
		return;
	}

	/* a receive event was triggered, accept incoming packets and process.
	 * we might have already processed all packets and got no new ones while
	 * the receive event was sitting in the scheduler. */
	if(list_get_size(vt_mgr->inq->buffer) < 1) {
		/* we've reached the end of our chain-receive. no more packets for now.
		 * any new arrivals can now immediately fire a recv event */
		vt_mgr->ok_to_fire_recv = 1;
		return;
	} else {
		/* we will chain recv events, incoming packets should not fire until we
		 * have taken our bandwidth penalty that we compute below. */
		vt_mgr->ok_to_fire_recv = 0;
	}

	debugf("vtransport_mgr_download_next: looking for transport items to receive\n");

	/* adjust ns cpu counter */
//	vtransport_mgr_adjust_cpu_load_counter(vt_mgr);

	/* adjust ns bandwidth counter */
	uint64_t ns_since_last = VTRANSPORT_NS_PER_MS * (global_sim_context.sim_worker->current_time - vt_mgr->last_time_recv);
	if(ns_since_last > 0) {
		if(vt_mgr->nanos_consumed_recv > ns_since_last) {
			/* we partially absorbed the delay */
			vt_mgr->nanos_consumed_recv -= ns_since_last;
		} else {
			/* enough time has passed that we absorbed the entire delay */
			vt_mgr->nanos_consumed_recv = 0;
		}
		vt_mgr->last_time_recv = global_sim_context.sim_worker->current_time;
	}

	/* we will batch recvs */
	list_tp titems_to_process = list_create();
	while (vt_mgr->nanos_consumed_recv < VTRANSPORT_MGR_BATCH_TIME &&
			list_get_size(vt_mgr->inq->buffer) > 0) {
		vtransport_item_tp titem = list_pop_front(vt_mgr->inq->buffer);
		if(titem == NULL) {
			dlogf(LOG_CRIT, "vtransport_mgr_download_next: incoming titem is NULL\n");
			vtransport_destroy_item(titem);
			continue;
		}

		vpacket_tp packet = vpacket_mgr_lockcontrol(titem->rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

		if(packet == NULL) {
			dlogf(LOG_CRIT, "vtransport_mgr_download_next: incoming packet is NULL\n");
			vtransport_destroy_item(titem);
			continue;
		}

		/* we free up some buffer space */
		vt_mgr->inq->current_size -= packet->data_size;

		vpacket_mgr_lockcontrol(titem->rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

		/* add to the list of items that will be processed this round */
		list_push_back(titems_to_process, titem);

		/* update consumed bandwidth */
		uint32_t effective_size = vpacket_get_size(titem->rc_packet);
		vt_mgr->nanos_consumed_recv += effective_size * vt_mgr->nanos_per_byte_down;
	}

	/* notify transport it has packets to process */
	vtransport_process_incoming_items(vt_mgr->vsocket_mgr, titems_to_process);

	/* list of items better be empty */
	if(list_get_size(titems_to_process) > 0) {
		dlogf(LOG_CRIT, "vtransport_mgr_download_next: not all packets processed by vsocket\n");
	}

	list_destroy(titems_to_process);

	/* now we have a cpu delay counter and a receive delay counter.
	 * we are constrained by the slower (larger) of these. */
//	uint64_t actual_delay = vt_mgr->nanos_consumed_recv > vt_mgr->nanos_cpu_delay ?
//			vt_mgr->nanos_consumed_recv : vt_mgr->nanos_cpu_delay;

	uint64_t actual_delay = vt_mgr->nanos_consumed_recv;

#if 0
	if(vt_mgr->nanos_consumed_recv > vt_mgr->nanos_accumulated_delay) {
		debugf("vtransport_mgr_download_next: constrained by network speed (net delay = %lu, cpu delay = %lu)\n", vt_mgr->nanos_consumed_recv, vt_mgr->nanos_accumulated_delay);
	} else if(vt_mgr->nanos_accumulated_delay > 0) {
		debugf("vtransport_mgr_download_next: constrained by CPU speed (net delay = %lu, cpu delay = %lu)\n", vt_mgr->nanos_consumed_recv, vt_mgr->nanos_accumulated_delay);
	}
#endif

	/* if it doesnt take a millisecond, we cant schedule an event */
	if(actual_delay >= VTRANSPORT_NS_PER_MS) {
		/* callback after delays */
		uint32_t msdelay = actual_delay / VTRANSPORT_NS_PER_MS;
		vci_schedule_downloaded(vt_mgr->vsocket_mgr->addr, msdelay);
	} else {
		/* not enough delays for a full MS */
		vt_mgr->ok_to_fire_recv = 1;
	}
}

void vtransport_mgr_ready_send(vtransport_mgr_tp vt_mgr, vsocket_tp sock) {
	/* dont add the socket if its already in the list!
	 * TODO list should implement a contains() method instead */
	uint8_t do_add = 1;
	if(list_get_size(vt_mgr->ready_to_send) > 0) {
		list_tp new_ready_to_send = list_create();

		while(list_get_size(vt_mgr->ready_to_send) > 0) {
			uint32_t* sockdp = list_pop_front(vt_mgr->ready_to_send);
			if(*sockdp == sock->sock_desc){
				do_add = 0;
			}
			list_push_back(new_ready_to_send, sockdp);
		}
		list_destroy(vt_mgr->ready_to_send);

		vt_mgr->ready_to_send = new_ready_to_send;
	}

	if(do_add) {
		uint32_t* sockdp = malloc(sizeof(uint32_t));
		*sockdp = sock->sock_desc;
		list_push_back(vt_mgr->ready_to_send, sockdp);
	}

	/* trigger a send event if this is the first ready buffer */
	if (vt_mgr->ok_to_fire_send) {
		vtransport_mgr_upload_next(vt_mgr);
	}
}

void vtransport_mgr_upload_next(vtransport_mgr_tp vt_mgr) {
	/* a send event was triggered, we should send some data
	 * from the front of send list.
	 * there might not be any ready buffers if no data was written while
	 * the send event was sitting in the scheduler. */
	if(list_get_size(vt_mgr->ready_to_send) < 1) {
		/* we've reached the end of our chain-send. no more packets for now.
		 * any new arrivals can now immediately fire a send event */
		vt_mgr->ok_to_fire_send = 1;
		return;
	} else {
		/* we will chain send events, incoming packets should not fire until we
		 * have taken our bandwidth penalty that we compute below. */
		vt_mgr->ok_to_fire_send = 0;
	}

	debugf("vtransport_mgr_upload_next: looking for packets to send\n");

	/* adjust ns cpu counter */
//	vtransport_mgr_adjust_cpu_load_counter(vt_mgr);

	/* adjust ns bandwidth counter */
	uint64_t ns_since_last = VTRANSPORT_NS_PER_MS * (global_sim_context.sim_worker->current_time - vt_mgr->last_time_sent);
	if(ns_since_last > 0) {
		if(vt_mgr->nanos_consumed_sent > ns_since_last) {
			/* we partially absorbed the delay */
			vt_mgr->nanos_consumed_sent -= ns_since_last;
		} else {
			/* enough time has passed that we absorbed the entire delay */
			vt_mgr->nanos_consumed_sent = 0;
		}
		vt_mgr->last_time_sent = global_sim_context.sim_worker->current_time;
	}

	/* we will batch sends */
	uint16_t num_transmitted = 0;
	while (vt_mgr->nanos_consumed_sent < VTRANSPORT_MGR_BATCH_TIME &&
			list_get_size(vt_mgr->ready_to_send) > 0) {
		/* we do round robin on all ready sockets */
		uint32_t* sockdp = list_pop_front(vt_mgr->ready_to_send);
		vsocket_tp sock = vsocket_mgr_get_socket(vt_mgr->vsocket_mgr, *sockdp);
		if(sock == NULL || sock->vt == NULL) {
			debugf("vtransport_mgr_upload_next: send buffer NULL during round robin, maybe socket %i closed\n", *sockdp);
			continue;
		}

		uint32_t bytes_transmitted = 0;
		uint16_t packets_remaining = 0;
		uint8_t was_transmitted = vtransport_transmit(sock->vt, &bytes_transmitted, &packets_remaining);

		if(was_transmitted) {
			/* update bandwidth consumed */
			vt_mgr->nanos_consumed_sent += bytes_transmitted * vt_mgr->nanos_per_byte_up;
			num_transmitted++;
		}

		/* if send_buffer has more, return it to round robin queue */
		if(was_transmitted && packets_remaining > 0) {
			list_push_back(vt_mgr->ready_to_send, sockdp);
		} else {
			free(sockdp);
		}
	}

	/* now we have a cpu delay counter and a receive delay counter.
	 * we are constrained by the slower (larger) of these. */
//	uint64_t actual_delay = vt_mgr->nanos_consumed_sent > vt_mgr->nanos_cpu_delay ?
//			vt_mgr->nanos_consumed_sent : vt_mgr->nanos_cpu_delay;

	uint64_t actual_delay = vt_mgr->nanos_consumed_sent;

#if 0
	if(vt_mgr->nanos_consumed_sent > vt_mgr->nanos_accumulated_delay) {
		debugf("vtransport_mgr_upload_next: constrained by network speed (net delay = %lu, cpu delay = %lu)\n", vt_mgr->nanos_consumed_sent, vt_mgr->nanos_accumulated_delay);
	} else if(vt_mgr->nanos_accumulated_delay > 0) {
		debugf("vtransport_mgr_upload_next: constrained by CPU speed (net delay = %lu, cpu delay = %lu)\n", vt_mgr->nanos_consumed_sent, vt_mgr->nanos_accumulated_delay);
	}
#endif

	/* if it doesnt take a millisecond, we cant schedule an event */
	if(num_transmitted > 0 && actual_delay >= VTRANSPORT_NS_PER_MS) {
		/* callback after absorbing delays */
		uint32_t msdelay = actual_delay / VTRANSPORT_NS_PER_MS;
		vci_schedule_uploaded(vt_mgr->vsocket_mgr->addr, msdelay);
	} else {
		/* we didnt send enough for a full MS */
		vt_mgr->ok_to_fire_send = 1;
	}
}

void vtransport_mgr_onpacket(vtransport_mgr_tp vt_mgr, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);

	/* called by vci when there is an incoming packet. */
	debugf("vtransport_mgr_onpacket: event fired\n");

	if(vt_mgr != NULL) {
		vsocket_tp sock = vsocket_mgr_get_socket_receiver(vt_mgr->vsocket_mgr, rc_packet);
		if(sock != NULL) {
			vtransport_mgr_ready_receive(vt_mgr, sock, rc_packet);
		} else {
			debugf("socket no longer exists, dropping packet\n");
		}
	}

	rc_vpacket_pod_release_stack(rc_packet);
}

void vtransport_mgr_onuploaded(vtransport_mgr_tp vt_mgr) {
	debugf("vtransport_mgr_onuploaded: event fired\n");
	vtransport_mgr_upload_next(vt_mgr);
}

void vtransport_mgr_ondownloaded(vtransport_mgr_tp vt_mgr) {
	debugf("vtransport_mgr_ondownloaded: event fired\n");
	vtransport_mgr_download_next(vt_mgr);
}
