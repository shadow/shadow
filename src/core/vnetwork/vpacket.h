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

#ifndef VPACKET_H_
#define VPACKET_H_

#include <netinet/in.h>
#include <stdint.h>

#include "shmcabinet_mgr.h"
#include "rwlock_mgr.h"
#include "list.h"

#ifdef DEBUG
#define vpacket_log_debug(packet) vpacket_log(packet)
#else
#define vpacket_log_debug(packet)
#endif

/* ! Changing the format of structs in this file will require an update
 * to the encode and decode functions in vci.c.
 */

/* network header sizes */
#define VPACKET_IP_HEADER_SIZE 20
#define VPACKET_TCP_HEADER_SIZE 20
#define VPACKET_UDP_HEADER_SIZE 8

#define VSOCKET_TCP_MSS 1460
#define VSOCKET_UDP_MSS 1472
/* the largest amount possible for any data segment in a packet */
#define VPACKET_MSS 1472

enum vpacket_pod_flags {
	VP_NONE=0, VP_OWNED=1, VP_SHARED=2,
};

enum vpacket_lockcontrol {
	LC_NONE=0,
	LC_OP_READLOCK=1, LC_OP_READUNLOCK=2, LC_OP_WRITELOCK=4, LC_OP_WRITEUNLOCK=8,
	LC_TARGET_PACKET=32, LC_TARGET_PAYLOAD=64
};

enum vpacket_tcp_flags {
	FIN = 1, SYN = 2, RST = 4, ACK = 8, CON = 16
};

typedef struct vpacket_tcp_header_s {
	/* contains tcp specifics, like seq #s, etc */
	uint32_t sequence_number;
	uint32_t acknowledgement;
	uint32_t advertised_window;
	enum vpacket_tcp_flags flags;
} vpacket_tcp_header_t, *vpacket_tcp_header_tp;

typedef struct vpacket_header_s {
	/* source information */
	in_addr_t source_addr;
	in_port_t source_port;
	/* destination information */
	in_addr_t destination_addr;
	in_port_t destination_port;
	/* SOCK_DGRAM or SOCK_STREAM */
	uint8_t protocol;
} vpacket_header_t, *vpacket_header_tp;

typedef struct vpacket_s {
	/* all packets have a header */
	vpacket_header_t header;
	/* additional header for SOCK_STREAM packets */
	vpacket_tcp_header_t tcp_header;
	/* application data */
	uint16_t data_size;
	void* payload;
} vpacket_t, *vpacket_tp;

typedef struct vpacket_pod_s {
	enum vpacket_pod_flags pod_flags;
	struct vpacket_mgr_s* vp_mgr;
	vpacket_tp vpacket;

	/* shm items only used if using shared mem */
	shm_item_tp shmitem_packet;
	shm_item_tp shmitem_payload;

	/* these locks are only used if we are locking reg packets */
	/* TODO wrap these in items so we can avoid deadlocks similar to
	 * the shmcabinet_mgr read and write functions. */
	rwlock_mgr_tp packet_lock;
	rwlock_mgr_tp payload_lock;
} vpacket_pod_t, *vpacket_pod_tp;

/* packet pods are wrapped by a reference counter */
typedef void (*rc_vpacket_pod_destructor_fp)(vpacket_pod_tp vpacket_pod);
typedef struct rc_vpacket_pod_s {
	vpacket_pod_tp pod;
	int8_t reference_count;
	rc_vpacket_pod_destructor_fp destructor;
} rc_vpacket_pod_t, *rc_vpacket_pod_tp;

rc_vpacket_pod_tp rc_vpacket_pod_create(vpacket_pod_tp vp_pod, rc_vpacket_pod_destructor_fp destructor);
vpacket_pod_tp rc_vpacket_pod_get(rc_vpacket_pod_tp rc_vpacket_pod);
void rc_vpacket_pod_retain(rc_vpacket_pod_tp rc_vpacket_pod);
void rc_vpacket_pod_release(rc_vpacket_pod_tp rc_vpacket_pod);
#define rc_vpacket_pod_retain_stack(rc_vpacket_pod) rc_vpacket_pod_retain(rc_vpacket_pod)
#define rc_vpacket_pod_release_stack(rc_vpacket_pod) rc_vpacket_pod_release(rc_vpacket_pod)

vpacket_tp vpacket_set(vpacket_tp vpacket, uint8_t protocol, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, enum vpacket_tcp_flags flags,
		uint32_t seq_number, uint32_t ack_number, uint32_t advertised_window,
		uint16_t data_size, const void* data);
uint32_t vpacket_get_size(rc_vpacket_pod_tp rc_packet);
void vpacket_log(rc_vpacket_pod_tp vpacket_pod);

#endif /* VPACKET_H_ */
