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

#ifndef VUDP_H_
#define VUDP_H_

#include <netinet/in.h>
#include <stddef.h>

#include "vsocket_mgr.h"
#include "vsocket.h"
#include "vtransport_processing.h"
#include "vbuffer.h"

/* -maximum size data we can send network:
 *	-udp data we can send is 65507, otherwise EMSGSIZE is returned
 */
#define VSOCKET_MAX_DGRAM_SIZE 65507

typedef struct vudp_s {
	vsocket_mgr_tp vsocket_mgr;
	vsocket_tp sock;
	vbuffer_tp vb;
	vpeer_tp default_remote_peer;
}vudp_t, *vudp_tp;

vudp_tp vudp_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, vbuffer_tp vb);
void vudp_destroy(vudp_tp vudp);
enum vt_prc_result vudp_process_item(vtransport_item_tp titem);
ssize_t vudp_recv(vsocket_mgr_tp net, vsocket_tp udpsock, void* dest_buf, size_t n, in_addr_t* addr, in_port_t* port);
ssize_t vudp_send(vsocket_mgr_tp net, vsocket_tp udpsock, const void* src_buf, size_t n, in_addr_t addr, in_port_t port);
uint8_t vudp_send_packet(vudp_tp vudp, rc_vpacket_pod_tp rc_packet);
rc_vpacket_pod_tp vudp_wire_packet(vudp_tp vudp);

#endif /* VUDP_H_ */
