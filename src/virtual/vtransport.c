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

#include <glib.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

#include "shadow.h"

vtransport_tp vtransport_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock) {
	vtransport_tp vt = malloc(sizeof(vtransport_t));

	vt->vsocket_mgr = vsocket_mgr;
	vt->sock = sock;

	guint64 wmem = CONFIG_SEND_BUFFER_SIZE;
	guint64 rmem = CONFIG_RECV_BUFFER_SIZE;

	/* this is now done in vtcp_autotune */
#if 0
	if(!CONFIG_SEND_BUFFER_SIZE_FORCE) {
		/* our receive buffer needs to be large enough to allow the sender to send
		 * a full delay*bandwidth worth of bytes to keep the pipe full. */
		guint64 max_latency_milliseconds = global_sim_context.sim_worker->max_latency;
		guint64 worst_case_rtt_milliseconds = max_latency_milliseconds * 2;
		gdouble delay_seconds = ((gdouble)worst_case_rtt_milliseconds) / 1000.0;

		/* we need bandwidth in bytes per second */
		guint64 bandwidth_bytes_per_second_up = vsocket_mgr->vt_mgr->KBps_up * 1024;
		guint64 delay_bandwidth_product = delay_seconds * bandwidth_bytes_per_second_up;

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

vtransport_item_tp vtransport_create_item(guint16 sockd, rc_vpacket_pod_tp rc_packet) {
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

void vtransport_process_incoming_items(vsocket_mgr_tp net, GQueue *titems) {
	if(titems != NULL) {
		/* we need to process the entire list of packets, storing them as needed. */
		while(g_queue_get_length(titems) > 0) {
			vtransport_item_tp titem = g_queue_pop_head(titems);

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
					info("vtransport_process_incoming_items: ignoring packet for non-existent socket (was it deleted?)\n");
				}
			} else {
				warning("vtransport_process_incoming_items: transport item is NULL, can not process\n");
			}

			vtransport_destroy_item(titem);
		}
	}
}

guint8 vtransport_is_empty(vtransport_tp vt) {
	return vbuffer_is_empty(vt->vb);
}

guint8 vtransport_transmit(vtransport_tp vt, guint32* bytes_transmitted, guint16* packets_remaining) {
	rc_vpacket_pod_tp rc_packet = NULL;
	guint32 bytes_consumed = 0;
	guint8 was_transmitted = 0;

	/* get packet, how is protocol specific */
	rc_packet = vt->sock->type == SOCK_STREAM ?
			vtcp_wire_packet(vt->vtcp) :
			vudp_wire_packet(vt->vudp);

	/* send the packet */
	if(rc_packet != NULL) {
		debug("vtransport_transmit: sending packet for socket %i\n", vt->sock->sock_desc);
		vpacket_log_debug(rc_packet);

		/* FIXME each interface should be separated and have its own bandwidth
		 * values and queue sizes. since they dont, loopback likely will buffer
		 * too much data, fill its queue, and not run as fast as it should.
		 */
		if(rc_packet->pod != NULL && rc_packet->pod->vpacket != NULL &&
				rc_packet->pod->vpacket->header.destination_addr == htonl(INADDR_LOOPBACK)) {
			PacketArrivedEvent* event = packetarrived_new(rc_packet);
			worker_scheduleEvent((Event*)event, 1, vt->vsocket_mgr->addr);
		} else {
			network_schedulePacket(rc_packet);
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
