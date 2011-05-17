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

#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "vsocket_mgr.h"
#include "vtransport.h"
#include "vtransport_processing.h"
#include "vtransport_mgr.h"
#include "vsocket.h"
#include "hashtable.h"
#include "log.h"
#include "vci.h"
#include "sysconfig.h"
#include "sim.h"
#include "vepoll.h"

vtransport_tp vtransport_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock) {
	vtransport_tp vt = malloc(sizeof(vtransport_t));

	vt->vsocket_mgr = vsocket_mgr;
	vt->sock = sock;

	uint64_t wmem = sysconfig_get_int("vnetwork_send_buffer_size");
	uint64_t rmem = sysconfig_get_int("vnetwork_recv_buffer_size");

	/* this is now done in vtcp_autotune */
#if 0
	int force = sysconfig_get_int("vnetwork_send_buffer_size_force");
	if(force == 0) {
		/* our receive buffer needs to be large enough to allow the sender to send
		 * a full delay*bandwidth worth of bytes to keep the pipe full. */
		uint64_t max_latency_milliseconds = global_sim_context.sim_worker->max_latency;
		uint64_t worst_case_rtt_milliseconds = max_latency_milliseconds * 2;
		double delay_seconds = ((double)worst_case_rtt_milliseconds) / 1000.0;

		/* we need bandwidth in bytes per second */
		uint64_t bandwidth_bytes_per_second_up = vsocket_mgr->vt_mgr->KBps_up * 1024;
		uint64_t delay_bandwidth_product = delay_seconds * bandwidth_bytes_per_second_up;

		/* only adjust it if we need more space */
		if(rmem < delay_bandwidth_product) {
			rmem = delay_bandwidth_product;
		}
	}
#endif

	vt->vb = vbuffer_create(sock->type, rmem, wmem, sock->vep);

	if(sock->type == SOCK_STREAM) {
		vt->vtcp = vtcp_create(vsocket_mgr, sock, vt->vb);
		vt->vudp = NULL;
	} else {
		vt->vtcp = NULL;
		vt->vudp = vudp_create(vsocket_mgr, sock, vt->vb);
	}

	return vt;
}

void vtransport_destroy(vtransport_tp vt) {
	if(vt != NULL) {
		vbuffer_destroy(vt->vb);
		vt->vb = NULL;
		vtcp_destroy(vt->vtcp);
		vt->vtcp = NULL;
		vudp_destroy(vt->vudp);
		vt->vudp = NULL;

		vt->vsocket_mgr = NULL;
		vt->sock = NULL;

		free(vt);
	}
}

vtransport_item_tp vtransport_create_item(uint16_t sockd, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);

	vtransport_item_tp titem = malloc(sizeof(vtransport_item_t));

	titem->sockd = sockd;
	titem->sock = NULL;
	titem->rc_packet = rc_packet;
	rc_vpacket_pod_retain(rc_packet);

	rc_vpacket_pod_release_stack(rc_packet);
	return titem;
}

void vtransport_destroy_item(vtransport_item_tp titem) {
	if(titem != NULL) {
		rc_vpacket_pod_release(titem->rc_packet);
		free(titem);
	}
}

void vtransport_process_incoming_items(vsocket_mgr_tp net, list_tp titems) {
	if(titems != NULL) {
		/* we need to process the entire list of packets, storing them as needed. */
		while(list_get_size(titems) > 0) {
			vtransport_item_tp titem = list_pop_front(titems);

			if(titem != NULL){
				titem->sock = vsocket_mgr_get_socket(net, titem->sockd);
				if(titem->sock != NULL) {
					enum vt_prc_result prc_result;

					/* process the packet */
					if(titem->sock->type == SOCK_STREAM) {
						prc_result = vtcp_process_item(titem);
					} else {
						prc_result = vudp_process_item(titem);
					}

					/* take action from processing result, if not destroyed */
					if(!(prc_result & VT_PRC_DESTROY) && !(prc_result & VT_PRC_RESET)) {
						if(prc_result & VT_PRC_WRITABLE) {
							vepoll_mark_available(titem->sock->vep, VEPOLL_WRITE);
						}
						if(prc_result & VT_PRC_READABLE) {
							vepoll_mark_available(titem->sock->vep, VEPOLL_READ);
						}
						if(prc_result & VT_PRC_PARENT_READABLE) {
							vsocket_tp parent_sock = vsocket_mgr_get_socket(titem->sock->vt->vsocket_mgr, titem->sock->sock_desc_parent);
							vepoll_mark_available(parent_sock->vep, VEPOLL_READ);
						}
						if(prc_result & VT_PRC_SENDABLE) {
							vtransport_mgr_ready_send(titem->sock->vt->vsocket_mgr->vt_mgr, titem->sock);
						}
					}
				} else {
					dlogf(LOG_INFO, "vtransport_process_incoming_items: ignoring packet for non-existent socket (was it deleted?)\n");
				}
			} else {
				dlogf(LOG_WARN, "vtransport_process_incoming_items: transport item is NULL, can not process\n");
			}

			vtransport_destroy_item(titem);
		}
	}
}

void vtransport_onretransmit(vsocket_mgr_tp net, in_addr_t dst_addr, in_port_t dst_port,
		in_port_t src_port, uint32_t retransmit_key) {
	debugf("vtransport_onretransmit: event fired\n");

	if(net == NULL) {
		return;
	}

	debugf("vtransport_onretransmit: %s:%u requesting retransmission of %u from %s:%u\n",
			inet_ntoa_t(dst_addr), ntohs(dst_port), retransmit_key, net->addr_string, ntohs(src_port));

	vsocket_tp sock = vsocket_mgr_find_socket(net, SOCK_STREAM, dst_addr, dst_port, src_port);
	if(sock == NULL || sock->vt == NULL) {
		return;
	}

	if(sock->vt->vtcp != NULL && sock->vt->vtcp->remote_peer == NULL) {
		dlogf(LOG_INFO, "vtransport_onretransmit: %s:%u has no connected child socket. was it closed?\n",
				inet_ntoa_t(net->addr), ntohs(src_port));
		return;
	}

	vtcp_retransmit(sock->vt->vtcp, retransmit_key);
}

void vtransport_onclose(vsocket_mgr_tp net, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, uint64_t rcv_end) {
	debugf("vsocket_mgr_onclose: event fired\n");

	vsocket_tp sock = vsocket_mgr_find_socket(net, SOCK_STREAM, src_addr, src_port, dst_port);
	if(sock != NULL && sock->vt != NULL && sock->vt->vtcp != NULL) {
		if(sock->curr_state == VTCP_CLOSING) {
			/* we initiated a close, other end got all data and scheduled this event */
			vsocket_transition(sock, VTCP_CLOSED);
			vsocket_mgr_destroy_and_remove_socket(net, sock);
		} else if(sock->curr_state == VTCP_LISTEN) {
			/* some other end is closing, we are listening so we do not care.
			 * probably this means that the child that this was actually meant for
			 * was already deleted, so vsocket_mgr_find_socket returned the
			 * parent listener instead. just ignore. */
			return;
		} else {
			/* other end is initiating a close */
			vsocket_transition(sock, VTCP_CLOSE_WAIT);
			sock->vt->vtcp->rcv_end = rcv_end;

			/* we should close after client reads all remaining data */
			sock->do_delete = 1;

			/* other end will not accept any more data */
			vbuffer_clear_send(sock->vt->vb);
			vbuffer_clear_tcp_retransmit(sock->vt->vb, 0, 0);

			/* and we are done, but have to wait to get everything from network
			 * and then for client to read EOF */
			if(rcv_end <= sock->vt->vtcp->rcv_nxt) {
				/* we already got everything they will send, tell them they should close */
				vci_schedule_close(net->addr, dst_addr, dst_port, src_addr, src_port, 0);

				/* tell vepoll that we are ready to read EOF */
				vepoll_mark_available(sock->vep, VEPOLL_READ);
			}
		}
	}
}

uint8_t vtransport_is_empty(vtransport_tp vt) {
	return vbuffer_is_empty(vt->vb);
}

uint8_t vtransport_transmit(vtransport_tp vt, uint32_t* bytes_transmitted, uint16_t* packets_remaining) {
	rc_vpacket_pod_tp rc_packet = NULL;
	uint32_t bytes_consumed = 0;
	uint8_t was_transmitted = 0;

	/* get packet, how is protocol specific */
	rc_packet = vt->sock->type == SOCK_STREAM ?
			vtcp_wire_packet(vt->vtcp) :
			vudp_wire_packet(vt->vudp);

	/* send the packet */
	if(rc_packet != NULL) {
		debugf("vtransport_transmit: sending packet for socket %i\n", vt->sock->sock_desc);
		vpacket_log_debug(rc_packet);

		/* FIXME each interface should be separated and have its own bandwidth
		 * values and queue sizes. since they dont, loopback likely will buffer
		 * too much data, fill its queue, and not run as fast as it should.
		 */
		if(rc_packet->pod != NULL && rc_packet->pod->vpacket != NULL &&
				rc_packet->pod->vpacket->header.destination_addr == htonl(INADDR_LOOPBACK)) {
			vci_schedule_packet_loopback(rc_packet, vt->vsocket_mgr->addr);
		} else {
			vci_schedule_packet(rc_packet);
			bytes_consumed = vpacket_get_size(rc_packet);
		}

		was_transmitted = 1;
		rc_vpacket_pod_release(rc_packet);
	} else {
		/* i wasn't able to send either because there are no
		 * more packets, or the packets are being throttled.
		 */
	}

	if(bytes_transmitted != NULL) {
		*bytes_transmitted = bytes_consumed;
	}
	if(packets_remaining != NULL) {
		*packets_remaining = vbuffer_get_send_length(vt->vb);
	}

	return was_transmitted;
}
