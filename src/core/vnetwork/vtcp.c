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
#include <stddef.h>
#include <string.h>
#include <netinet/in.h>
#include <errno.h>

#include "vtcp.h"
#include "vtransport.h"
#include "vtransport_processing.h"
#include "vtransport_mgr.h"
#include "vtcp_server.h"
#include "vsocket_mgr_server.h"
#include "vbuffer.h"
#include "vpeer.h"
#include "vpacket_mgr.h"
#include "vpacket.h"
#include "orderedlist.h"
#include "vci.h"
#include "log.h"
#include "sysconfig.h"
#include "resolver.h"
#include "sim.h"

static void vtcp_autotune(vtcp_tp vtcp);
static void vtcp_update_receive_window(vtcp_tp vtcp);
static void vtcp_demultiplex_child(vsocket_tp sock, in_addr_t addr, in_port_t port);
static vtcp_server_child_tp vtcp_multiplex_child(vtcp_server_tp server, in_addr_t addr, in_port_t port);
static enum vt_prc_result vtcp_process_state(vsocket_tp sock, rc_vpacket_pod_tp rc_packet);
static enum vt_prc_result vtcp_process_updates(vsocket_tp sock, rc_vpacket_pod_tp rc_packet);
static enum vt_prc_result vtcp_process_data(vsocket_tp sock, rc_vpacket_pod_tp rc_packet);
static enum vt_prc_result vtcp_process_data_helper(vsocket_tp sock, rc_vpacket_pod_tp rc_packet);
static void vtcp_trysend_dack(vtcp_tp vtcp);
static uint8_t vtcp_update_perceived_congestion(vtcp_tp vtcp, uint32_t bytes, uint8_t timeout);
static uint8_t vtcp_update_send_window(vtcp_tp vtcp);
static uint8_t vtcp_update_unacknowledged(vtcp_tp vtcp, uint64_t acknum);

vtcp_tp vtcp_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, vbuffer_tp vb){
	vtcp_tp vtcp = malloc(sizeof(vtcp_t));

	/* TODO make config option */
	uint32_t initial_window = 10;

	vtcp->sock = sock;
	vtcp->remote_peer = NULL;

	sock->curr_state = VTCP_CLOSED;
	sock->prev_state = VTCP_CLOSED;
	vtcp->connection_was_reset = 0;

	vtcp->cng_wnd = initial_window;
	vtcp->cng_threshold = 0;
	vtcp->is_slow_start = 1;
	vtcp->last_adv_wnd = initial_window;

	uint32_t iss = vtcp_generate_iss();
	vtcp->rcv_end = 0;
	vtcp->snd_end = iss;
	vtcp->snd_dack = 0;

	vtcp->snd_una = iss;
	vtcp->snd_nxt = 0;
	vtcp->snd_wnd = initial_window;
	vtcp->snd_wl1 = 0;
	vtcp->snd_wl2 = 0;
	vtcp->rcv_nxt = 0;
	vtcp->rcv_wnd = initial_window;
	vtcp->rcv_irs = 0;

	vtcp->vsocket_mgr = vsocket_mgr;
	vtcp->vb = vb;

	return vtcp;
}

void vtcp_destroy(vtcp_tp vtcp){
	if(vtcp != NULL){
		vtcp_disconnect(vtcp);

		memset(vtcp, 0, sizeof(vtcp_t));
		free(vtcp);
	}
}

void vtcp_connect(vtcp_tp vtcp, in_addr_t remote_addr, in_port_t remote_port) {
	vtcp->remote_peer = vpeer_create(remote_addr, remote_port);
}

void vtcp_disconnect(vtcp_tp vtcp) {
	vpeer_destroy(vtcp->remote_peer);
	vtcp->remote_peer = NULL;
}

ssize_t vtcp_send(vsocket_mgr_tp net, vsocket_tp tcpsock, const void* src_buf, size_t n) {
	uint16_t packet_size = VTRANSPORT_MTU;
	uint16_t packet_header_size = VPACKET_IP_HEADER_SIZE + VPACKET_TCP_HEADER_SIZE;
	uint16_t packet_data_size = packet_size - packet_header_size;

	/* we accept at most VTRANSPORT_TCP_MAX_STREAM_SIZE from user */
	size_t data_bytes = n < VTRANSPORT_TCP_MAX_STREAM_SIZE ? n : VTRANSPORT_TCP_MAX_STREAM_SIZE;
	size_t bytes_sent = 0;
	size_t copy_size = 0;

	/* calculate how many bytes we can send */
	size_t sendable_data_bytes = vbuffer_send_space_available(tcpsock->vt->vb);
	size_t remaining = sendable_data_bytes < data_bytes ? sendable_data_bytes : data_bytes;

	/* break data into segments, and send each in a packet */
	while (remaining > 0) {
		/* does the remaining data bytes fit in a packet */
		if(remaining < packet_data_size) {
			copy_size = remaining;
		} else {
			/* copy_size + headers will be a full MTU */
			copy_size = packet_data_size;
		}

		/* create the actual packet */
		rc_vpacket_pod_tp rc_packet = vtcp_create_packet(tcpsock->vt->vtcp, ACK,
				(uint16_t) copy_size, src_buf + bytes_sent);

		/* attempt to store the packet in transport */
		uint8_t success = vtcp_send_packet(tcpsock->vt->vtcp, rc_packet);

		/* release our stack copy of the pointer */
		rc_vpacket_pod_release(rc_packet);

		if(!success) {
			dlogf(LOG_WARN, "vtcp_send: unable to send packet\n");
			return bytes_sent;
		}

		bytes_sent += copy_size;
		remaining -= copy_size;
	}

	debugf("vtcp_send: sent %i bytes to transport\n", bytes_sent);

	return (ssize_t) bytes_sent;
}

uint8_t vtcp_send_packet(vtcp_tp vtcp, rc_vpacket_pod_tp rc_packet) {
	uint8_t success = 0;
	if(rc_packet != NULL) {
		rc_vpacket_pod_retain_stack(rc_packet);
		vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

		if(packet != NULL) {
			/* add the packet to the send buffer, then have vtransport_mgr check
			 * if we can send another one based on our send window, etc */
			if(packet->data_size > 0) {
				uint64_t key = packet->tcp_header.sequence_number;
				vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
				success = vbuffer_add_send(vtcp->vb, rc_packet, key);
			} else {
				vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
				success = vbuffer_add_control(vtcp->vb, rc_packet);
			}
			vtransport_mgr_ready_send(vtcp->vsocket_mgr->vt_mgr, vtcp->sock);
		} else {
			dlogf(LOG_CRIT, "vtcp_send_packet: trying to send NULL packet\n");
			rc_vpacket_pod_release(rc_packet);
		}

		rc_vpacket_pod_release_stack(rc_packet);
	}
	return success;
}

ssize_t vtcp_recv(vsocket_mgr_tp net, vsocket_tp tcpsock, void* dest_buf, size_t n) {
	ssize_t remaining = n;
	size_t bytes_read = 0;
	size_t copy_size = 0;
	void* copy_start = NULL;
	uint16_t* read_offset = NULL;

	while(remaining > 0) {
		/* get the next packet for this socket */
		rc_vpacket_pod_tp rc_packet = vbuffer_get_read(tcpsock->vt->vb, &read_offset);
		vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET | LC_TARGET_PAYLOAD);

		if(packet == NULL) {
			/* our copy of the rc_packet will be deleted upon return */
			rc_vpacket_pod_release(rc_packet);

			/* no more data to read */
			if(bytes_read <= 0){
				errno = EAGAIN;
				return VSOCKET_ERROR;
			} else {
				return bytes_read;
			}
		}

		/* we may have already read part of this packet */
		uint8_t partial = remaining < packet->data_size - *read_offset ? 1 : 0;

		/* compute where and how much to copy */
		copy_start = packet->payload + *read_offset;
		copy_size = partial == 1 ? remaining : packet->data_size - *read_offset;

		/* copy to app buffer */
		memcpy(dest_buf + bytes_read, copy_start, copy_size);
		bytes_read += copy_size;

		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET| LC_TARGET_PAYLOAD);

		/* cleanup operations */
		if(partial) {
			/* just did partial read of the packet */
			*read_offset += copy_size;
			remaining = 0;
		} else {
			/* just read the entire unread packet contents */
			*read_offset = 0;
			remaining -= copy_size;
			/* we should remove transports copy of the packet */
			rc_vpacket_pod_tp rc_packet_copy = vbuffer_remove_read(tcpsock->vt->vb);
			rc_vpacket_pod_release(rc_packet_copy);
		}

		/* done with rc_packet, it will be out of scope */
		rc_vpacket_pod_release(rc_packet);
	}

	return bytes_read;
}

enum vt_prc_result vtcp_process_item(vtransport_item_tp titem) {
	enum vt_prc_result prc_result = VT_PRC_NONE;

	if(titem == NULL || titem->rc_packet == NULL) {
		goto ret;
	}

	vsocket_tp target = vtcp_get_target_socket(titem);

	/* we must have a socket */
	if(target == NULL) {
		dlogf(LOG_INFO, "vtcp_process_item: ignoring NULL target socket (child socket was destroyed?)\n");
		goto ret;
	} else if (target->ethernet_peer == NULL && target->loopback_peer == NULL) {
		dlogf(LOG_WARN, "vtcp_process_item: cannot process unbound socket\n");
		goto ret;
	}

	vpacket_tp packet = vpacket_mgr_lockcontrol(titem->rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

	/* must have packet and header info to proceed */
	if (packet == NULL) {
		dlogf(LOG_WARN, "vtcp_process_item: cannot process without incoming control packet\n");
		goto ret;
	} else if (packet->header.protocol != SOCK_STREAM) {
		dlogf(LOG_WARN, "vtcp_process_item: cannot process without incoming control header\n");
		goto ret;
	} else if(target->vt == NULL || target->vt->vtcp == NULL) {
		dlogf(LOG_WARN, "vtcp_process_item: cannot process without connection\n");
		goto ret;
	}

	rc_vpacket_pod_retain_stack(titem->rc_packet);
	rc_set(titem->rc_packet);

	vpacket_mgr_lockcontrol(titem->rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

	debugf("vtcp_process_item: socket %i got seq# %u from %s\n", target->sock_desc, packet->tcp_header.sequence_number, inet_ntoa_t(packet->header.source_addr));

	prc_result |= vtcp_process_state(target, titem->rc_packet);

	if(prc_result & VT_PRC_RESET) {
		goto done;
	}

	prc_result |= vtcp_process_updates(target, titem->rc_packet);
	if(!(prc_result & VT_PRC_DROPPED)) {
		prc_result |= vtcp_process_data(target, titem->rc_packet);
	}

	debugf("vtcp_process_item: socket %i cngthresh=%u, cngwnd=%u, snduna=%u, sndnxt=%u, sndwnd=%u, rcvnxt=%u, rcvwnd=%u\n",
			target->sock_desc,
			target->vt->vtcp->cng_threshold, target->vt->vtcp->cng_wnd,
			target->vt->vtcp->snd_una, target->vt->vtcp->snd_nxt, target->vt->vtcp->snd_wnd,
			target->vt->vtcp->rcv_nxt, target->vt->vtcp->rcv_wnd);

	if(prc_result & VT_PRC_DESTROY) {
		vsocket_mgr_destroy_and_remove_socket(target->vt->vtcp->vsocket_mgr, target);
	}

done:
	rc_vpacket_pod_release_stack(titem->rc_packet);
	rc_unset(titem->rc_packet);

ret:
	return prc_result;
}

static void vtcp_reset(vtcp_tp vtcp, vsocket_tp sock, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);


	/* error: connection reset */
	if(sock->curr_state == VTCP_SYN_RCVD){
		/* clear all segments in retransmission queue */
		vbuffer_clear_tcp_retransmit(vtcp->vb, 0, 0);

		if(sock->prev_state == VTCP_LISTEN) {
			/* initiated with passive open, return to listen */
			vsocket_transition(sock, VTCP_LISTEN);

			/* delete the multiplexed connection thats not a server */
			/* TODO can we call vsocket_mgr_destroy_and_remove_socket on sock? */
			if(sock->sock_desc_parent != 0) {
				vsocket_tp parent_sock = vsocket_mgr_get_socket(vtcp->vsocket_mgr, sock->sock_desc_parent);
				if(parent_sock != NULL) {
					vtcp_server_tp server = vsocket_mgr_get_server(vtcp->vsocket_mgr, parent_sock);
					if(server != NULL) {
						vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
						if(packet != NULL) {
							vtcp_server_child_tp schild = vtcp_server_get_child(server, packet->header.source_addr, packet->header.source_port);
							if(schild != NULL) {
								vsocket_mgr_destroy_and_remove_socket(vtcp->vsocket_mgr, schild->sock);
							}
							vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
						}
					}
				}
			}
		}

		if(sock->prev_state == VTCP_SYN_SENT) {
			/* initiated with active open, connection was refused */
			sock->do_delete = 1;
			vsocket_transition(sock, VTCP_CLOSED);
			vsocket_mgr_try_destroy_socket(vtcp->vsocket_mgr, sock);
		}
	} else if(sock->curr_state == VTCP_CLOSING) {
		/* client already called close, other side reset */
		vsocket_mgr_destroy_and_remove_socket(vtcp->vsocket_mgr, sock);
	} else {
		vtcp->connection_was_reset = 1;
		sock->do_delete = 1;
		vsocket_transition(sock, VTCP_CLOSED);
		vsocket_mgr_try_destroy_socket(vtcp->vsocket_mgr, sock);
	}

	rc_vpacket_pod_release_stack(rc_packet);
}

static enum vt_prc_result vtcp_process_state(vsocket_tp sock, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	enum vt_prc_result prc_result = VT_PRC_NONE;

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet == NULL) {
		goto ret2;
	}
	vsocket_mgr_tp vs = sock->vt->vsocket_mgr;
	vtcp_tp vtcp = sock->vt->vtcp;
	vpacket_header_tp hdr = &packet->header;
	vpacket_tcp_header_tp tcphdr = &packet->tcp_header;
	enum vpacket_tcp_flags flags = tcphdr->flags;

	if(flags & RST) {
		vtcp_reset(vtcp, sock, rc_packet);
		prc_result |= VT_PRC_RESET;
		goto ret;
	}

	switch (sock->curr_state) {

		case VTCP_CLOSED:
			vtcp_send_control_packet(vtcp, RST);
			prc_result |= VT_PRC_DROPPED;
			break;

		case VTCP_LISTEN:
			if ((flags & SYN) && (flags & CON)) {
				/* step 2 of handshake: send SYN+ACK */
				vtcp->rcv_irs = tcphdr->sequence_number;
				vtcp->rcv_nxt = vtcp->rcv_irs + 1;
				vtcp->snd_nxt = vtcp->snd_una = VSOCKET_ISS;

				vtcp_send_control_packet(vtcp, SYN | ACK | CON);
				vsocket_transition(sock, VTCP_SYN_RCVD);

				/* avoid double increment in postprocess */
				vtcp->rcv_nxt--;
			} else {
				/* only SYNs are valid */
				vtcp_send_control_packet(vtcp, RST);

				/* multiplexed child expected a SYN, so destroy it */
				vtcp_demultiplex_child(sock, vtcp->remote_peer->addr, vtcp->remote_peer->port);
				prc_result |= VT_PRC_DROPPED;
			}
			break;

		case VTCP_SYN_SENT:
			if(flags & ACK) {
				if(tcphdr->acknowledgement < VSOCKET_ISS ||
						tcphdr->acknowledgement > vtcp->snd_nxt) {
					/* ack not in acceptable range */
					vtcp_send_control_packet(vtcp, RST);
					prc_result |= VT_PRC_DROPPED;
					break;
				}
			}

			if((flags & SYN) && (flags & CON)) {
				vtcp->rcv_irs = tcphdr->sequence_number;
				vtcp->rcv_nxt = vtcp->rcv_irs + 1;

				if(flags & ACK) {
					/* step 3 of handshake */
					vtcp_send_control_packet(vtcp, ACK | CON);
					vsocket_transition(sock, VTCP_ESTABLISHED);
					vtcp_autotune(vtcp);
					/* we are connected, client may write */
					prc_result |= VT_PRC_WRITABLE;
				} else {
					/* simultaneous open */
					vsocket_transition(sock, VTCP_SYN_RCVD);
					vtcp_send_control_packet(vtcp, SYN | ACK | CON);
				}

				/* avoid double increment in postprocess */
				vtcp->rcv_nxt--;
			}
			break;

		case VTCP_SYN_RCVD:
		case VTCP_ESTABLISHED:
		case VTCP_CLOSING:
		case VTCP_CLOSE_WAIT:

			/* check if packet is in range */
			if (tcphdr->sequence_number < vtcp->rcv_nxt ||
					tcphdr->sequence_number >= vtcp->rcv_nxt + vtcp->rcv_wnd) {
				/* not in acceptable range now, source should retransmit later.
				 * we only care about future packets or packets with data */
				if(packet->data_size > 0 || tcphdr->sequence_number > vtcp->rcv_nxt) {
					vci_schedule_retransmit(rc_packet, vs->addr);
				}
				prc_result |= VT_PRC_DROPPED;
				break;
			}

			if (flags & SYN) {
				/* we should not be receiving SYNs at this point */
				vtcp_send_control_packet(vtcp, RST);
				vtcp_reset(vtcp, sock, rc_packet);
				prc_result |= VT_PRC_DROPPED;
				break;
			}

			if ((flags & ACK) && (flags & CON) && sock->curr_state == VTCP_SYN_RCVD) {
				/* got ACK from handshake step 3, both sides established */
				vsocket_transition(sock, VTCP_ESTABLISHED);
				vtcp_autotune(vtcp);

				/* this is a previously incomplete multiplexed server connection */
				if(sock->sock_desc_parent != 0) {
					vsocket_tp parent_sock = vsocket_mgr_get_socket(vs, sock->sock_desc_parent);
					vtcp_server_tp server = vsocket_mgr_get_server(vs, parent_sock);
					vtcp_server_child_tp schild = vtcp_server_get_child(server, hdr->source_addr, hdr->source_port);
					if(schild != NULL) {
						vtcp_server_remove_child_incomplete(server, schild);
						uint8_t success = vtcp_server_add_child_pending(server, schild);

						if(success) {
							/* server should accept connection */
							prc_result |= VT_PRC_PARENT_READABLE;
						} else {
							/* no space to hold pending connection */
							dlogf(LOG_WARN, "vtcp_preprocess: server has too many connections, dropping new connection request!\n");
							vtcp_send_control_packet(vtcp, RST);
							vtcp_reset(vtcp, sock, rc_packet);
							prc_result |= VT_PRC_DROPPED;
							break;
						}
					} else {
						dlogf(LOG_CRIT, "vtcp_preprocess: unable to process newly established multiplexed connection\n");
					}
				} else {
					dlogf(LOG_CRIT, "vtcp_preprocess: no parent for multiplexed connection\n");
				}
			}
			break;

		default:
			debugf("vtcp_preprocess: dropping packet received while in state %i\n", sock->curr_state);
			break;
	}

ret:
	vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
ret2:
	rc_vpacket_pod_release_stack(rc_packet);
	return prc_result;
}

static enum vt_prc_result vtcp_process_updates(vsocket_tp sock, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	enum vt_prc_result prc_result = VT_PRC_NONE;

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

	if(packet != NULL && sock != NULL && sock->vt != NULL) {
		vpacket_tcp_header_tp tcphdr = &packet->tcp_header;
		vtcp_tp vtcp = sock->vt->vtcp;

		/* congestion and flow control */
		if(tcphdr->acknowledgement > vtcp->snd_una && tcphdr->acknowledgement <= vtcp->snd_nxt) {
			/* keep track of how many packets just got acked */
			int64_t packets_acked = tcphdr->acknowledgement - vtcp->snd_una;

			/* advance snd_una */
			if(vtcp_update_unacknowledged(vtcp, tcphdr->acknowledgement)) {
				prc_result |= VT_PRC_SENDABLE;
			}

			/* update window, prevent old segments from updating window */
			if(vtcp->snd_wl1 < tcphdr->sequence_number ||
					(vtcp->snd_wl1 == tcphdr->sequence_number &&
					vtcp->snd_wl2 <= tcphdr->acknowledgement)) {
				vtcp->last_adv_wnd = tcphdr->advertised_window;

				/* keep track of when window was updated */
				vtcp->snd_wl1 = tcphdr->sequence_number;
				vtcp->snd_wl2 = tcphdr->acknowledgement;

				prc_result |= VT_PRC_SENDABLE;
			}

			/* update cng_wnd and snd_wnd */
			if(vtcp_update_perceived_congestion(vtcp, packets_acked, 0)) {
				prc_result |= VT_PRC_SENDABLE;
			}

			if(sock->curr_state == VTCP_CLOSING && vtcp->snd_una >= vtcp->snd_end) {
				/* everything i needed to send before closing was acknowledged */
				prc_result |= VT_PRC_DESTROY;
			}
		} else if (tcphdr->acknowledgement <= vtcp->snd_una){
			/* duplicate ACK ignored */
		}

		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
	}

	rc_vpacket_pod_release_stack(rc_packet);
	return prc_result;
}

static enum vt_prc_result vtcp_process_data(vsocket_tp sock, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	enum vt_prc_result prc_result = VT_PRC_NONE;

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

	if(packet != NULL) {
		uint32_t seqnum = packet->tcp_header.sequence_number;
		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

		vtcp_tp vtcp = sock->vt->vtcp;

		/* process data in-order */
		if(seqnum == vtcp->rcv_nxt) {
			prc_result |= vtcp_process_data_helper(sock, rc_packet);

			/* the previous packet may have filled in some gaps */
			rc_vpacket_pod_tp rc_packet_gap;
			while ((rc_packet_gap = vbuffer_remove_tcp_unprocessed(vtcp->vb, vtcp->rcv_nxt)) != NULL) {
				prc_result |= vtcp_process_data_helper(sock, rc_packet_gap);
				rc_vpacket_pod_release(rc_packet_gap);
			}
		} else {
			/* buffer and process out of order data later */
			if(!vbuffer_add_receive(vtcp->vb, rc_packet)) {
				/* no buffer space, sender should retransmit */
				vci_schedule_retransmit(rc_packet, vtcp->vsocket_mgr->addr);
				prc_result |= VT_PRC_DROPPED;
			}
		}
	}

	rc_vpacket_pod_release_stack(rc_packet);
	return prc_result;
}


static enum vt_prc_result vtcp_process_data_helper(vsocket_tp sock, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	enum vt_prc_result prc_result = VT_PRC_NONE;

	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet != NULL) {
		vtcp_tp vtcp = sock->vt->vtcp;
		uint16_t datasize = packet->data_size;

		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

		if(datasize > 0){
			/* process packet data */
			if(sock->curr_state == VTCP_ESTABLISHED || sock->curr_state == VTCP_CLOSE_WAIT) {
				if(!vbuffer_add_read(sock->vt->vb, rc_packet)) {
					/* no buffer space, sender should retransmit */
					vci_schedule_retransmit(rc_packet, sock->vt->vsocket_mgr->addr);
					prc_result |= VT_PRC_DROPPED;
					/* avoid updating rcv_nxt, we are not actually accepting packet */
					goto ret;
				}
				prc_result |= VT_PRC_READABLE;
			}
		}

		/* if we got here, we have space to store packet */
		vtcp->rcv_nxt++;
		debugf("vtcp_process_data_helper: socket %i advance seq# %u from %s\n", sock->sock_desc, packet->tcp_header.sequence_number, inet_ntoa_t(packet->header.source_addr));

		packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

		/* notify other end that we received packet, either an ack, or an
		 * event in case we are closing.
		 */
		if(sock->curr_state == VTCP_CLOSE_WAIT &&
				vtcp->rcv_end != 0 && vtcp->rcv_nxt >= vtcp->rcv_end) {
			/* other end will close, send event and not ack */
			vci_schedule_close(sock->vt->vsocket_mgr->addr, packet->header.destination_addr, packet->header.destination_port,
					packet->header.source_addr, packet->header.source_port, 0);
		} else if((packet->tcp_header.flags & ACK) && packet->data_size > 0) {
			vtcp_trysend_dack(vtcp);
		}

		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
	}

ret:
	rc_vpacket_pod_release_stack(rc_packet);
	return prc_result;
}

static uint8_t vtcp_update_perceived_congestion(vtcp_tp vtcp, uint32_t packets_acked, uint8_t timeout) {
	if(vtcp != NULL) {
		if(timeout) {
			/* this is basically a negative ack.
			 * handle congestion control.
			 * TCP-Reno-like fast retransmit, i.e. multiplicative decrease. */
			vtcp->cng_wnd /= 2;
			if(vtcp->cng_wnd < 1) {
				vtcp->cng_wnd = 1;
			}
			if(vtcp->is_slow_start && vtcp->cng_threshold == 0) {
				vtcp->cng_threshold = vtcp->cng_wnd;
			}
		} else {
			if(vtcp->is_slow_start) {
				/* threshold not set => no timeout yet => slow start phase 1
				 *  i.e. multiplicative increase until retransmit event (which sets threshold)
				 * threshold set => timeout => slow start phase 2
				 *  i.e. multiplicative increase until threshold */
				vtcp->cng_wnd += packets_acked;
				if(vtcp->cng_threshold != 0 && vtcp->cng_wnd >= vtcp->cng_threshold) {
					vtcp->is_slow_start = 0;
				}
			} else {
				/* slow start is over
				 * simple additive increase part of AIMD */
				vtcp->cng_wnd += packets_acked * packets_acked / vtcp->cng_wnd;
			}
		}
		return vtcp_update_send_window(vtcp);
	}
	return 0;
}

static vtcp_server_child_tp vtcp_multiplex_child(vtcp_server_tp server, in_addr_t addr, in_port_t port) {
	/* server will multiplex a child socket */
	vtcp_server_child_tp schild = vtcp_server_create_child(server, addr, port);

	if(schild != NULL) {
		/* tell server to manage connection */
		vtcp_server_add_child_incomplete(server, schild);

		/* configure the connection */
		vtcp_connect(schild->sock->vt->vtcp, addr, port);
		/* dont use transition here, since that changes the child to active
		 * but its not really active until accepted.
		 */
		schild->sock->prev_state = schild->sock->curr_state;
		schild->sock->curr_state = VTCP_LISTEN;
	}

	return schild;
}

static void vtcp_demultiplex_child(vsocket_tp sock, in_addr_t addr, in_port_t port) {
	if(sock != NULL && sock->vt != NULL) {
		vsocket_tp parent = vsocket_mgr_get_socket(sock->vt->vsocket_mgr, sock->sock_desc_parent);
		vtcp_server_tp server = vsocket_mgr_get_server(sock->vt->vsocket_mgr, parent);
		vtcp_server_child_tp schild = vtcp_server_get_child(server, addr, port);

		if(schild != NULL) {
			/* configure the connection */
			vtcp_disconnect(schild->sock->vt->vtcp);
			vsocket_transition(schild->sock, VTCP_CLOSED);

			/* update server */
			vtcp_server_destroy_child(server, schild);
		}
	}
}

vsocket_tp vtcp_get_target_socket(vtransport_item_tp titem) {
	/* find a target socket for fsm processing. the packet could be a
	 * new connection request, which means we need to create it */
	vsocket_tp target = NULL;

	if(titem != NULL && titem->sock != NULL && titem->sock->vt != NULL) {
		/* servers need to multiplex a client socket */
		vtcp_server_tp server = vsocket_mgr_get_server(titem->sock->vt->vsocket_mgr, titem->sock);
		if(server == NULL) {
			/* socket is not a server, target is original socket */
			target = titem->sock;
		} else {
			/* socket is a server */
			vpacket_tp packet = vpacket_mgr_lockcontrol(titem->rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

			if(packet != NULL) {
				uint8_t do_multiplex = 0;
				if(packet->header.destination_addr == htonl(INADDR_LOOPBACK)) {
					if(titem->sock->loopback_peer != NULL) {
						if(titem->sock->loopback_peer->port == packet->header.destination_port &&
							packet->tcp_header.flags == (SYN | CON)) {
							do_multiplex = 1;
						}
					}
				} else {
					if(titem->sock->ethernet_peer != NULL) {
						if(titem->sock->ethernet_peer->port == packet->header.destination_port &&
							packet->tcp_header.flags == (SYN | CON)) {
							do_multiplex = 1;
						}
					}
				}

				if(do_multiplex) {
					/* server will multiplex a child socket */
					vtcp_server_child_tp schild = vtcp_multiplex_child(server, packet->header.source_addr, packet->header.source_port);
					if(schild != NULL) {
						target = schild->sock;
					}
				}
				vpacket_mgr_lockcontrol(titem->rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
			}
		}
	}

	if(target == NULL) {
		debugf("vtcp_get_target_socket: unable to locate target socket, maybe socket closed\n");
	}
	return target;
}

void vtcp_send_control_packet(vtcp_tp vtcp, enum vpacket_tcp_flags flags) {
	rc_vpacket_pod_tp rc_control_packet = vtcp_create_packet(vtcp, flags, 0, NULL);

	if(!vtcp_send_packet(vtcp, rc_control_packet)) {
		/* this should never happen since control packets take no buffer space */
		dlogf(LOG_CRIT, "vtcp_send_control_packet: cannot send control packet\n");
	}

	rc_vpacket_pod_release(rc_control_packet);
}

static uint8_t vtcp_update_unacknowledged(vtcp_tp vtcp, uint64_t acknum) {
	/* we only update to largest ack */
	if(acknum > vtcp->snd_una) {
		vtcp->snd_una = acknum;
		vbuffer_clear_tcp_retransmit(vtcp->vb, 1, acknum);
		/* window slid, try to send more */
		return 1;
	}
	return 0;
}

static uint8_t vtcp_update_send_window(vtcp_tp vtcp) {
	uint32_t old_window = vtcp->snd_wnd;

	/* send window is minimum of congestion window and advertised/old send window */
	vtcp->snd_wnd = vtcp->last_adv_wnd < vtcp->cng_wnd ? vtcp->last_adv_wnd : vtcp->cng_wnd;
	if(vtcp->snd_wnd < 1) {
		vtcp->snd_wnd = 1;
	}

	/* do we want to TCP re-tune here to dynamically shrink buffers as window closes and opens?.
	 * make sure we have enough buffer space to handle a full send window */

	if(vtcp->snd_wnd > old_window) {
		/* window opened, try to send more */
		return 1;
	}
	return 0;
}

static void vtcp_autotune(vtcp_tp vtcp) {
	if(vtcp != NULL) {
		int force = sysconfig_get_int("vnetwork_send_buffer_size_force");
		if(force == 0) {
			if(vtcp->remote_peer->addr == htonl(INADDR_LOOPBACK)) {
				/* 16 MiB as max */
				vbuffer_set_size(vtcp->vb, 16777216, 16777216);
				debugf("vtcp_autotune: set loopback buffer sizes to 16777216\n");
				return;
			}

			/* our buffers need to be large enough to send and receive
			 * a full delay*bandwidth worth of bytes to keep the pipe full. */
			uint32_t send_latency, receive_latency;

			/* get latency in milliseconds */
			uint8_t success = vci_get_latency(vtcp->vsocket_mgr->addr, vtcp->remote_peer->addr, &send_latency, &receive_latency);
			if(!success) {
				dlogf(LOG_WARN, "vtcp_autotune: cant get latency for autotuning. defaulting to worst case latency.\n");
				send_latency = global_sim_context.sim_worker->max_latency;
				receive_latency = global_sim_context.sim_worker->max_latency;
			}

			uint32_t rtt_milliseconds = send_latency + receive_latency;

			/* i got delay, now i need values for my send and receive buffer
			 * sizes based on bandwidth in both directions.
			 * do my send size first. */
			uint32_t my_send_bw = vtcp->vsocket_mgr->vt_mgr->KBps_up;
			uint32_t their_receive_bw = resolver_get_downbw(global_sim_context.sim_worker->resolver, vtcp->remote_peer->addr);
			uint32_t my_send_Bpms = (uint32_t) (my_send_bw * 1.024f);
			uint32_t their_receive_Bpms = (uint32_t) (their_receive_bw * 1.024f);

			uint32_t send_bottleneck_bw = my_send_Bpms < their_receive_Bpms ? my_send_Bpms : their_receive_Bpms;

			/* the delay bandwidth product is how many bytes I can send at once to keep the pipe full.
			 * mult. by 1.2 to account for network overhead. */
			uint64_t sendbuf_size = (uint64_t) (rtt_milliseconds * send_bottleneck_bw * 1.25f);

			/* now the same thing for my receive buf */
			uint32_t my_receive_bw = vtcp->vsocket_mgr->vt_mgr->KBps_down;
			uint32_t their_send_bw = resolver_get_upbw(global_sim_context.sim_worker->resolver, vtcp->remote_peer->addr);
			uint32_t my_receive_Bpms = (uint32_t) (my_receive_bw * 1.024f);
			uint32_t their_send_Bpms = (uint32_t) (their_send_bw * 1.024f);

			uint32_t receive_bottleneck_bw;
			if(their_send_Bpms > my_receive_Bpms) {
				receive_bottleneck_bw = their_send_Bpms;
			} else {
				receive_bottleneck_bw = my_receive_Bpms;
			}

			if(their_send_Bpms - my_receive_Bpms > -4096 || their_send_Bpms - my_receive_Bpms < 4096) {
				receive_bottleneck_bw = (uint32_t) (receive_bottleneck_bw * 1.2f);
			}

			/* the delay bandwidth product is how many bytes I can receive at once to keep the pipe full */
			uint64_t receivebuf_size = (uint64_t) (rtt_milliseconds * receive_bottleneck_bw * 1.25);

			vbuffer_set_size(vtcp->vb, receivebuf_size, sendbuf_size);
			debugf("vtcp_autotune: set network buffer sizes: send %lu receive %lu\n", sendbuf_size, receivebuf_size);
		}
	}
}

static void vtcp_trysend_dack(vtcp_tp vtcp) {
	/* fixme add this to config */
	if(sysconfig_get_int("usedack") == 1) {
		/* in practice, there is an ack delay timer of 40ms. the empty ack isn't
		 * sent until the timer expires if app data does not comes in. this
		 * prevents sending an ack when you could have piggybacked it soon after.
		 * The socket layer tries to guess when to use this, and can get it
		 * wrong meaning it can actually reduce performance. If the apps are
		 * chatty, its a good idea, if data mostly flows one-way, bad idea. */
		if(vtcp != NULL && vtcp->sock != NULL) {
			/* set a timer and remember if an ack is piggybacked before the timer */
			vtcp->snd_dack = vtcp->snd_dack | dack_requested;
			/* if a dack is not currently scheduled, schedule one and set the bit */
			if(!(vtcp->snd_dack & dack_scheduled)) {
				vci_schedule_dack(vtcp->vsocket_mgr->addr, vtcp->sock->sock_desc, VTRANSPORT_TCP_DACK_TIMER);
				vtcp->snd_dack = vtcp->snd_dack | dack_scheduled;
			}
		}
	} else {
		/* if not using delayed acks, always send an ack */
		vtcp_send_control_packet(vtcp, ACK);
	}
}

static void vtcp_update_receive_window(vtcp_tp vtcp) {
	if(vtcp != NULL) {
		size_t space = vbuffer_receive_space_available(vtcp->vb);
		size_t num_packets = space / VSOCKET_TCP_MSS;
		if(num_packets < UINT32_MAX) {
			vtcp->rcv_wnd = (uint32_t)num_packets;
		} else {
			vtcp->rcv_wnd = UINT32_MAX;
		}
		if(vtcp->rcv_wnd < 1) {
			vtcp->rcv_wnd = 1;
		}
	}
}

/* called by transport, looking for a packet to put on the wire */
rc_vpacket_pod_tp vtcp_wire_packet(vtcp_tp vtcp) {
	rc_vpacket_pod_tp rc_packet = NULL;

	if(vtcp != NULL) {
		/* new advertised window */
		vtcp_update_receive_window(vtcp);

		/* we wont release since we return the packet */
		if(!vbuffer_is_empty_send_control(vtcp->vb)) {
			/* always send control packets first, to propogate our latest ACK */
			rc_packet = vbuffer_remove_tcp_control(vtcp->vb);
		} else {
			uint64_t key = vtcp->snd_una + vtcp->snd_wnd;
			rc_packet = vbuffer_remove_send(vtcp->vb, key);
		}
		if(rc_packet != NULL){
			/* it is ok to send this packet */
			vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_WRITELOCK | LC_TARGET_PACKET);

			if(packet != NULL) {
				/* we always update the advertised window */
				packet->tcp_header.advertised_window = vtcp->rcv_wnd;

				/* ack number should always be updated if acking */
				if(packet->tcp_header.flags & ACK) {
					packet->tcp_header.acknowledgement = vtcp->rcv_nxt;

					/* since we are sending an ack, any delayed ack can be canceled */
					vtcp->snd_dack = vtcp->snd_dack & ~dack_requested;
				}

				/* save packet in retransmit queue until its received
				 * key will be seq# + data_size so we can check against acknum when removing */
				uint64_t retransmit_key = packet->tcp_header.sequence_number;
				vpacket_mgr_lockcontrol(rc_packet, LC_OP_WRITEUNLOCK | LC_TARGET_PACKET);
				if(!vbuffer_add_retransmit(vtcp->vb, rc_packet, retransmit_key)) {
					dlogf(LOG_CRIT, "vtcp_wire_packet: packet will not be reliable\n");
				}
			}
		}
#ifdef DEBUG
		else {
			if(vbuffer_get_send_length(vtcp->vb) > 0) {
				debugf("vtcp_wire_packet: throttled socket %i, send window extends to %u\n", vtcp->sock->sock_desc, vtcp->snd_una + vtcp->snd_wnd);
			}else {
				debugf("vtcp_wire_packet: no packet to send for socket %i\n", vtcp->sock->sock_desc);
			}
		}
#endif
	}

	return rc_packet;
}

void vtcp_retransmit(vtcp_tp vtcp, uint32_t retransmit_key) {
	/* update send window if needed */
	uint8_t window_opened = vtcp_update_perceived_congestion(vtcp, 0, 1);
	uint8_t is_retransmitted = 0;

	rc_vpacket_pod_tp rc_packet = vbuffer_remove_tcp_retransmit(vtcp->vb, retransmit_key);
	if(rc_packet != NULL) {
		is_retransmitted = vtcp_send_packet(vtcp, rc_packet);


		if(is_retransmitted) {
			debugf("vtcp_retransmit: enqueued seq# %u for retransmission!\n", retransmit_key);
		} else {
			dlogf(LOG_CRIT, "vtcp_retransmit: cant retransmit valid seq# %u!\n", retransmit_key);
		}
		rc_vpacket_pod_release(rc_packet);
	} else {
		/* this might happen if an old packet was already removed from the retransmit
		 * buffer because we received a newer ack that cleared it.
		 */
		uint16_t sockd = 0;
		if(vtcp->sock != NULL) {
			sockd = vtcp->sock->sock_desc;
		}
		dlogf(LOG_WARN, "vtcp_retransmit: socket %i cant retransmit seq# %u. it may have been sent, cleared from a newer ack, or the socket closed.\n", sockd, retransmit_key);
	}

	/* try to send, packet might be within send window even if buffer has more than 1 item */
	if(window_opened || is_retransmitted) {
		vtransport_mgr_ready_send(vtcp->vsocket_mgr->vt_mgr, vtcp->sock);
	}
}

uint32_t vtcp_generate_iss() {
	/* TODO do we need a ISS generator? (rfc793 pg26) */
	return VTRANSPORT_TCP_ISS;
}

void vtcp_ondack(vci_event_tp vci_event, vsocket_mgr_tp vs_mgr) {
	debugf("vtcp_ondack: event fired\n");
        if(vci_event->payload != NULL) {
            uint16_t sockd = *(uint16_t *)(vci_event->payload);

            /* a delayed ack timer expired, send ack if needed */
            vsocket_tp sock = vsocket_mgr_get_socket(vs_mgr, sockd);
            if(sock != NULL && sock->vt != NULL && sock->vt->vtcp != NULL) {
                vtcp_tp vtcp = sock->vt->vtcp;
                if(vtcp->snd_dack & dack_requested) {
                    vtcp_send_control_packet(vtcp, ACK);
                }
                /* unset the scheduled bit */
                vtcp->snd_dack = vtcp->snd_dack & ~dack_scheduled;
            }
        }
}

rc_vpacket_pod_tp vtcp_create_packet(vtcp_tp vtcp, enum vpacket_tcp_flags flags, uint16_t data_size, const void* data) {
	if(vtcp != NULL && vtcp->sock != NULL && vtcp->remote_peer != NULL) {
		in_addr_t dst_addr = vtcp->remote_peer->addr;
		in_port_t dst_port = vtcp->remote_peer->port;

		in_addr_t src_addr = 0;
		in_port_t src_port = 0;
		if(vtcp->remote_peer->addr == htonl(INADDR_LOOPBACK)) {
			if(vtcp->sock->loopback_peer != NULL) {
				src_addr = vtcp->sock->loopback_peer->addr;
				src_port = vtcp->sock->loopback_peer->port;
			} else {
				dlogf(LOG_ERR, "vtcp_create_packet: trying to send to loopback but have no local loopback peer\n");
				return NULL;
			}
		} else {
			if(vtcp->sock->ethernet_peer != NULL) {
				src_addr = vtcp->sock->ethernet_peer->addr;
				src_port = vtcp->sock->ethernet_peer->port;
			} else {
				dlogf(LOG_ERR, "vtcp_create_packet: trying to send to ethernet but have no local ethernet peer\n");
				return NULL;
			}
		}

		/* if the socket was a multiplexed server socket, the source of the
		 * packet should be the server port.
		 */
		if(vtcp->sock->sock_desc_parent != 0) {
			vsocket_tp parent = vsocket_mgr_get_socket(vtcp->vsocket_mgr, vtcp->sock->sock_desc_parent);
			if(parent != NULL) {
				if(vtcp->remote_peer->addr == htonl(INADDR_LOOPBACK)) {
					if(parent->loopback_peer != NULL) {
						src_addr = parent->loopback_peer->addr;
						src_port = parent->loopback_peer->port;
					} else {
						dlogf(LOG_ERR, "vtcp_create_packet: trying to send to loopback but have no local loopback parent\n");
						return NULL;
					}
				} else {
					if(parent->ethernet_peer != NULL) {
						src_addr = parent->ethernet_peer->addr;
						src_port = parent->ethernet_peer->port;
					} else {
						dlogf(LOG_ERR, "vtcp_create_packet: trying to send to ethernet but have no local ethernet parent\n");
						return NULL;
					}
				}
			}
		}

		vtcp_update_receive_window(vtcp);

		rc_vpacket_pod_tp created_rc_packet = vpacket_mgr_create_tcp(vtcp->vsocket_mgr->vp_mgr, src_addr, src_port, dst_addr, dst_port,
				flags, vtcp->snd_nxt, vtcp->rcv_nxt, vtcp->rcv_wnd, data_size, data);

		vtcp->snd_end++;
		vtcp->snd_nxt++;

		return created_rc_packet;
	} else {
		dlogf(LOG_ERR, "vtcp_create_packet: can not send response packet from unconnected socket\n");
		return NULL;
	}
}
