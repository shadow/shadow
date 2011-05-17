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

#ifndef VPACKET_MGR_H_
#define VPACKET_MGR_H_

#include "vpacket.h"
#include "shmcabinet_mgr.h"

typedef struct vpacket_mgr_s {
	/* shared mem packet locking is handled by shmem manager */
	int use_shmcabinet;
	shmcabinet_mgr_tp smc_mgr_packets;
	shmcabinet_mgr_tp smc_mgr_payloads;

	/* if set normal packets should be locked */
	int lock_regular_packets;
}vpacket_mgr_t, *vpacket_mgr_tp;

/* convenience macros for packet types */
#define vpacket_mgr_create_udp(vp_mgr, src_addr, src_port, dst_addr, dst_port, data_size, data) \
	vpacket_mgr_packet_create(vp_mgr, SOCK_DGRAM, src_addr, src_port, dst_addr, dst_port, 0, 0, 0, 0, data_size, data)

#define vpacket_mgr_create_tcp(vp_mgr, src_addr, src_port, dst_addr, dst_port, \
		flags, seq_number, ack_number, advertised_window,data_size, data) \
	vpacket_mgr_packet_create(vp_mgr, SOCK_STREAM, src_addr, src_port, dst_addr, dst_port,\
			flags, seq_number, ack_number, advertised_window, data_size, data)

vpacket_mgr_tp vpacket_mgr_create();
void vpacket_mgr_destroy(vpacket_mgr_tp vp_mgr);

rc_vpacket_pod_tp vpacket_mgr_packet_create(vpacket_mgr_tp vp_mgr, uint8_t protocol,
		in_addr_t src_addr, in_port_t src_port, in_addr_t dst_addr, in_port_t dst_port,
		enum vpacket_tcp_flags flags, uint32_t seq_number, uint32_t ack_number, uint32_t advertised_window,
		uint16_t data_size, const void* data);
rc_vpacket_pod_tp vpacket_mgr_empty_packet_create();
void vpacket_mgr_setup_locks(vpacket_pod_tp vp_pod);
rc_vpacket_pod_tp vpacket_mgr_attach_shared_packet(vpacket_mgr_tp vp_mgr,
		shmcabinet_info_tp shminfo_packet, uint32_t slot_id_packet,
		shmcabinet_info_tp shminfo_payload, uint32_t slot_id_payload);
vpacket_tp vpacket_mgr_lockcontrol(rc_vpacket_pod_tp rc_vp_pod, enum vpacket_lockcontrol command);
void vpacket_mgr_vpacket_pod_destructor_cb(vpacket_pod_tp vp_pod);

#endif /* VPACKET_MGR_H_ */
