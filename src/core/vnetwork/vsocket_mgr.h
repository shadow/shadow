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

#ifndef VSOCKET_MGR_H_
#define VSOCKET_MGR_H_

#include <stdint.h>
#include <netinet/in.h>
#include <glib-2.0/glib.h>

#include "global.h"
#include "context.h"
#include "vpacket_mgr.h"
#include "vpacket.h"
#include "vpeer.h"
#include "vpipe.h"
#include "linkedbuffer.h"
#include "vevent_mgr.h"
#include "vepoll.h"
#include "vcpu.h"
#include "vci_event.h"

#define VPIPE_ALIGN_TAG 0x3F

enum vsocket_state {
	VUDP, VTCP_CLOSED, VTCP_LISTEN, VTCP_SYN_SENT, VTCP_SYN_RCVD, VTCP_ESTABLISHED, VTCP_CLOSING, VTCP_CLOSE_WAIT
};

typedef struct vinterface_s {
	in_addr_t ip_address;
	/* hashtable<udp port, vsocket> */
	GHashTable *udp_vsockets;
	/* hashtable<tcp port, vsocket> */
	GHashTable *tcp_vsockets;
	/* hashtable<tcp port, tcpserver> */
	GHashTable *tcp_servers;
} vinterface_t, *vinterface_tp;

typedef struct vsocket_t {
	/* type of this socket, either SOCK_DGRAM, or SOCK_STREAM */
	uint8_t type;
	/* the socket descriptor, unique for each socket */
	uint16_t sock_desc;
	/* the local name of the socket, (address and port) */
	vpeer_tp ethernet_peer;
	/* the loopback interface, non-null if bound to loopback */
	vpeer_tp loopback_peer;
	/* socket transport layer */
	struct vtransport_s* vt;
	/* if set, the socket will be deleted when its buffers become empty */
	uint8_t do_delete;
	/* multiplexed sockets are child sockets of a server */
	uint16_t sock_desc_parent;
	/* socket states */
	enum vsocket_state prev_state;
	enum vsocket_state curr_state;
	/* keeps track of the state of the socket */
	vepoll_tp vep;
	/* either the child socket is accepted, or the parent socket is listening */
	uint8_t is_active;
}vsocket_t, *vsocket_tp;

typedef struct vsocket_mgr_s {
	in_addr_t addr;
	char addr_string[INET_ADDRSTRLEN];
	uint16_t next_sock_desc;
	uint16_t next_rnd_port;
	/* hashtable<socket descriptor, vsocket> */
	GHashTable *vsockets;
	vinterface_tp loopback;
	vinterface_tp ethernet;
	/* sockets that were previously deleted but not yet closed by app
	 * TODO: this should probably be a BST or something */
	GHashTable *destroyed_descs;
	struct vtransport_mgr_s* vt_mgr;
	vpipe_mgr_tp vpipe_mgr;
	vpacket_mgr_tp vp_mgr;
	vevent_mgr_tp vev_mgr;
	vcpu_tp vcpu;
}vsocket_mgr_t, *vsocket_mgr_tp;

vsocket_mgr_tp vsocket_mgr_create(context_provider_tp p, in_addr_t addr, uint32_t KBps_down, uint32_t KBps_up, uint64_t cpu_speed_Bps);
void vsocket_mgr_destroy(vsocket_mgr_tp net);
vsocket_tp vsocket_mgr_create_socket(vsocket_mgr_tp net, uint8_t type);
void vsocket_mgr_destroy_socket(vsocket_tp sock);
void vsocket_mgr_add_socket(vsocket_mgr_tp net, vsocket_tp sock);
vsocket_tp vsocket_mgr_get_socket(vsocket_mgr_tp net, uint16_t sockd);
void vsocket_mgr_remove_socket(vsocket_mgr_tp net, vsocket_tp sock);
void vsocket_mgr_map_socket_tcp(vsocket_mgr_tp net, vsocket_tp sock);
vsocket_tp vsocket_mgr_get_socket_tcp(vsocket_mgr_tp net, uint16_t port);
void vsocket_mgr_unmap_socket_tcp(vsocket_mgr_tp net, vsocket_tp sock);
void vsocket_mgr_map_socket_udp(vsocket_mgr_tp net, vsocket_tp sock);
vsocket_tp vsocket_mgr_get_socket_udp(vsocket_mgr_tp net, uint16_t port);
void vsocket_mgr_unmap_socket_udp(vsocket_mgr_tp net, vsocket_tp sock);
void vsocket_mgr_destroy_socket_cb(int keu, void* value, void *param);
void vsocket_mgr_destroy_and_remove_socket(vsocket_mgr_tp net, vsocket_tp sock);
void vsocket_mgr_destroy_and_remove_socket_cb(int key, void* value, void* param);
void vsocket_mgr_try_destroy_socket(vsocket_mgr_tp net, vsocket_tp sock);
vsocket_tp vsocket_mgr_get_socket_receiver(vsocket_mgr_tp net, rc_vpacket_pod_tp rc_packet);
vsocket_tp vsocket_mgr_find_socket(vsocket_mgr_tp net, uint8_t protocol,
		in_addr_t remote_addr, in_port_t remote_port, in_port_t local_port);

vinterface_tp vsocket_mgr_create_interface(vsocket_mgr_tp net, in_addr_t addr);
uint8_t vsocket_mgr_isbound_loopback(vsocket_mgr_tp net, in_port_t port);
uint8_t vsocket_mgr_isbound_ethernet(vsocket_mgr_tp net, in_port_t port);
void vsocket_mgr_bind_ethernet(vsocket_mgr_tp net, vsocket_tp sock, in_port_t bind_port);
void vsocket_mgr_bind_loopback(vsocket_mgr_tp net, vsocket_tp sock, in_port_t bind_port);

void vsocket_mgr_onnotify(vci_event_tp vci_event, vsocket_mgr_tp vs_mgr);

void vsocket_mgr_print_stat(vsocket_mgr_tp net, uint16_t sockd);

#endif /* VSOCKET_MGR_H_ */
