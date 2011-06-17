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

#ifndef VTRANSPORT_H_
#define VTRANSPORT_H_

#include <stdint.h>
#include <stddef.h>

#include "vsocket_mgr.h"
#include "vtransport_processing.h"
#include "vtcp.h"
#include "vudp.h"
#include "vpacket_mgr.h"
#include "vpacket.h"

/* maximum size of an IP packet without fragmenting over Ethernetv2 */
#define VTRANSPORT_MTU 1500

typedef struct vtransport_s {
	vsocket_mgr_tp vsocket_mgr;
	vsocket_tp sock;
	vbuffer_tp vb;
	vtcp_tp vtcp;
	vudp_tp vudp;
}vtransport_t, *vtransport_tp;

vtransport_tp vtransport_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock);
vtransport_item_tp vtransport_create_item(uint16_t sockd, rc_vpacket_pod_tp rc_packet);
void vtransport_destroy(vtransport_tp vt);
void vtransport_destroy_item(vtransport_item_tp titem);
void vsocket_execute_receive(vtransport_tp vt, list_tp rc_packets);
uint8_t vtransport_is_empty(vtransport_tp vt);
void vtransport_notify_readable_cb(void* value, int key);
void vtransport_notify_writable_cb(void* value, int key);
void vtransport_onclose(vsocket_mgr_tp net, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, uint64_t rcv_end);
void vtransport_onretransmit(vsocket_mgr_tp net, in_addr_t dst_addr, in_port_t dst_port,
		in_port_t src_port, uint32_t retransmit_key);
void vtransport_process_incoming_items(vsocket_mgr_tp net, list_tp titems);
uint8_t vtransport_transmit(vtransport_tp vt, uint32_t* bytes_transmitted, uint16_t* packets_remaining);

#endif /* VTRANSPORT_H_ */
