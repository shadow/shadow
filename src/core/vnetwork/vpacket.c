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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "vpacket.h"
#include "reference_counter.h"
#include "log.h"

extern vpacket_tp vpacket_mgr_lockcontrol(rc_vpacket_pod_tp rc_vp_pod, enum vpacket_lockcontrol command);
static void vpacket_tcp_flags_to_string(void* buffer, size_t size, enum vpacket_tcp_flags flags);

/* This function copies application data into a packet, which will be sent at
* vsocket's convenience. This is the only copy that happens until the receiver
* copies the data into the receiver application's buffer, unless distributed
* mode requires sending the data to another machine.
*
* vpacket must be NON-NULL, and it should point to an allocated packet whose contents
* will be set using the parameters of this method.
*/
vpacket_tp vpacket_set(vpacket_tp vpacket, uint8_t protocol, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, enum vpacket_tcp_flags flags,
		uint32_t seq_number, uint32_t ack_number, uint32_t advertised_window,
		uint16_t data_size, const void* data) {
	/* check for allocation */
	if(vpacket == NULL){
		dlogf(LOG_ERR, "vpacket_set: please provide NON-NULL pointer to a vpacket\n");
		return NULL;
	}

	/* fill in packet */
	vpacket->header.protocol = protocol;
	vpacket->header.source_addr = src_addr;
	vpacket->header.source_port = src_port;
	vpacket->header.destination_addr = dst_addr;
	vpacket->header.destination_port = dst_port;

	if(protocol == SOCK_STREAM) {
		vpacket->tcp_header.acknowledgement = ack_number;
		vpacket->tcp_header.advertised_window = advertised_window;
		vpacket->tcp_header.flags = flags;
		vpacket->tcp_header.sequence_number = seq_number;
	}

	vpacket->data_size = data_size;
	if(data_size > 0 && data != NULL && vpacket->payload != NULL) {
		/* copy the data payload to packet */
		memcpy(vpacket->payload, data, data_size);
	}

	return vpacket;
}

uint32_t vpacket_get_size(rc_vpacket_pod_tp rc_packet) {
	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(packet == NULL) {
		return 0;
	}

	rc_vpacket_pod_retain(rc_packet);

	uint32_t total_size = packet->data_size + VPACKET_IP_HEADER_SIZE;

	if(packet->header.protocol == SOCK_STREAM) {
		total_size += VPACKET_TCP_HEADER_SIZE;
	} else if (packet->header.protocol == SOCK_DGRAM) {
		total_size += VPACKET_UDP_HEADER_SIZE;
	}

	vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);

	rc_vpacket_pod_release(rc_packet);

	return total_size;
}

void vpacket_log(rc_vpacket_pod_tp vpacket_pod) {
	vpacket_tp vpacket = vpacket_mgr_lockcontrol(vpacket_pod, LC_OP_READLOCK | LC_TARGET_PACKET);
	if(vpacket != NULL) {
		char srcip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &vpacket->header.source_addr, srcip, sizeof(srcip));
		char dstip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &vpacket->header.destination_addr, dstip, sizeof(dstip));

		if(vpacket->header.protocol == SOCK_STREAM) {
			char flagstring[24];
			vpacket_tcp_flags_to_string(flagstring, sizeof(flagstring), vpacket->tcp_header.flags);

			debugf("vpacket_log: TCP from %s:%u to %s:%u %s seq#:%u ack#:%u win#:%u bytes:%u\n",
					srcip,
					ntohs(vpacket->header.source_port),
					dstip,
					ntohs(vpacket->header.destination_port),
					flagstring,
					vpacket->tcp_header.sequence_number,
					vpacket->tcp_header.acknowledgement,
					vpacket->tcp_header.advertised_window,
					vpacket->data_size);
		} else {
			debugf("vpacket_log: UDP from %s:%u to %s:%u bytes:%u\n",
					srcip,
					ntohs(vpacket->header.source_port),
					dstip,
					ntohs(vpacket->header.destination_port),
					vpacket->data_size);
		}

		vpacket_mgr_lockcontrol(vpacket_pod, LC_OP_READUNLOCK | LC_TARGET_PACKET);
	}
}

static void vpacket_tcp_flags_to_string(void* buffer, size_t size, enum vpacket_tcp_flags flags) {
	int written = 0;
	if(written < size && (flags & FIN)) {
		written += snprintf(buffer + written, size - written, "|FIN");
	}
	if(written < size && (flags & SYN)) {
		written += snprintf(buffer + written, size - written, "|SYN");
	}
	if(written < size && (flags & RST)) {
		written += snprintf(buffer + written, size - written, "|RST");
	}
	if(written < size && (flags & ACK)) {
		written += snprintf(buffer + written, size - written, "|ACK");
	}
	if(written < size && (flags & CON)) {
		written += snprintf(buffer + written, size - written, "|CON");
	}
	if(written < size) {
		written += snprintf(buffer + written, size - written, "|");
	}
}

rc_vpacket_pod_tp rc_vpacket_pod_create(vpacket_pod_tp vp_pod, rc_vpacket_pod_destructor_fp destructor) {
	return (rc_vpacket_pod_tp) rc_create((rc_object_tp) vp_pod, (rc_object_destructor_fp) destructor);
}

void rc_vpacket_pod_retain(rc_vpacket_pod_tp rc_vpacket_pod) {
	rc_retain((rc_object_tp)rc_vpacket_pod);
}

void rc_vpacket_pod_release(rc_vpacket_pod_tp rc_vpacket_pod) {
	rc_release((rc_object_tp)rc_vpacket_pod);
}

vpacket_pod_tp rc_vpacket_pod_get(rc_vpacket_pod_tp rc_vpacket_pod) {
	return (vpacket_pod_tp) rc_get((rc_object_tp) rc_vpacket_pod);
}
