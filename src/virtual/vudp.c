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
#include <stdlib.h>
#include <stddef.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>

#include "shadow.h"

vudp_tp vudp_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, vbuffer_tp vb){
	vudp_tp vudp = malloc(sizeof(vudp_t));

	vudp->sock = sock;
	vudp->vsocket_mgr = vsocket_mgr;
	vudp->vb = vb;
	vudp->default_remote_peer = NULL;

	return vudp;
}

void vudp_destroy(vudp_tp vudp){
	if(vudp != NULL){
		vudp->sock = NULL;
		vudp->vsocket_mgr = NULL;
		vudp->vb = NULL;
		free(vudp);
	}
}

/* this function builds a UDP packet and sends to the virtual node given by the
 * addr and port parameters. this function assumes that the socket is already
 * bound to a local port, no matter if that happened explicitly or implicitly.
 */
ssize_t vudp_send(vsocket_mgr_tp net, vsocket_tp udpsock,
		const gpointer src_buf, size_t n, in_addr_t addr, in_port_t port){
	guint16 packet_max_data_size = VSOCKET_MAX_DGRAM_SIZE;
	size_t bytes_sent = 0;
	size_t copy_size = 0;
	size_t remaining = n;

	/* is there enough space in transport */
	size_t avail = vbuffer_send_space_available(udpsock->vt->vb);
	if(avail < n) {
		return VSOCKET_ERROR;
	}

	/* break data ginto segments, and send each in a packet */
	while (remaining > 0) {
		copy_size = packet_max_data_size;
		/* does the remaining bytes fit in a packet */
		if(remaining < packet_max_data_size) {
			copy_size = remaining;
		}

		/* create the actual packet */
		in_addr_t src_addr = 0;
		in_port_t src_port = 0;
		if(addr == htonl(INADDR_LOOPBACK)) {
			if(udpsock->loopback_peer != NULL) {
				src_addr = udpsock->loopback_peer->addr;
				src_port = udpsock->loopback_peer->port;
			}
		} else {
			if(udpsock->ethernet_peer != NULL) {
				src_addr = udpsock->ethernet_peer->addr;
				src_port = udpsock->ethernet_peer->port;
			}
		}
		if(src_addr == 0) {
			error("vudp_send: no src information for udp datagram\n");
			return VSOCKET_ERROR;
		}
		rc_vpacket_pod_tp rc_packet = vpacket_mgr_create_udp(net->vp_mgr, src_addr, src_port, addr, port,
				(guint16) copy_size, src_buf + bytes_sent);

		/* attempt to store the packet */
		guint8 success = vudp_send_packet(udpsock->vt->vudp, rc_packet);

		/* release our stack copy of the pointer */
		rc_vpacket_pod_release(rc_packet);

		if(!success) {
			warning("vudp_send: unable to send packet\n");
			return bytes_sent;
		}

		bytes_sent += copy_size;
		remaining = n - bytes_sent;
	}

	debug("vudp_send: sent %i bytes to transport\n", bytes_sent);

	return (ssize_t) bytes_sent;
}

guint8 vudp_send_packet(vudp_tp vudp, rc_vpacket_pod_tp rc_packet) {
	guint8 success = vbuffer_add_send(vudp->vb, rc_packet, 0);
	if(success && vbuffer_get_send_length(vudp->vb) == 1) {
		/* we just became ready to send */
		vtransport_mgr_ready_send(vudp->vsocket_mgr->vt_mgr, vudp->sock);
	}
	return success;
}

ssize_t vudp_recv(vsocket_mgr_tp net, vsocket_tp udpsock, gpointer dest_buf, size_t n, in_addr_t* addr, in_port_t* port){
	/* get the next packet for this socket */
	rc_vpacket_pod_tp rc_packet = vbuffer_remove_read(udpsock->vt->vb);
	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET | LC_TARGET_PAYLOAD);

	if(packet == NULL) {
		/* our copy of the rc_packet will be deleted upon return */
		rc_vpacket_pod_release(rc_packet);
		errno = EAGAIN;
		return VSOCKET_ERROR;
	}

	/* copy lesser of requested and available amount to application buffer */
	size_t numbytes = n < packet->data_size ? n : packet->data_size;
	memcpy(dest_buf, packet->payload, numbytes);

	/* fill in address info */
	if (addr != NULL) {
		*addr = packet->header.source_addr;
	}
	if(port != NULL) {
		*port = packet->header.source_port;
	}

	vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET | LC_TARGET_PAYLOAD);

	/* destroy packet, throwing away any bytes not claimed by the app */
	rc_vpacket_pod_release(rc_packet);

	return numbytes;
}

enum vt_prc_result vudp_process_item(vtransport_item_tp titem) {
	/* udp data, packet just gets stored for user */
	guint8 success = vbuffer_add_read(titem->sock->vt->vb, titem->rc_packet);
	if(success) {
		return VT_PRC_READABLE;
	} else {
		return VT_PRC_NONE;
	}
}

/* called by transport, looking for a packet to put on the wire */
rc_vpacket_pod_tp vudp_wire_packet(vudp_tp vudp) {
	return vbuffer_remove_send(vudp->vb, 0);
}
