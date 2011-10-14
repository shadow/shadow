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

#include <glib.h>
#include <netinet/in.h>
#include <stdint.h>

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

struct vpacket_tcp_header_s {
	/* contains tcp specifics, like seq #s, etc */
	guint32 sequence_number;
	guint32 acknowledgement;
	guint32 advertised_window;
	enum vpacket_tcp_flags flags;
};

struct vpacket_header_s {
	/* source information */
	in_addr_t source_addr;
	in_port_t source_port;
	/* destination information */
	in_addr_t destination_addr;
	in_port_t destination_port;
	/* SOCK_DGRAM or SOCK_STREAM */
	guint8 protocol;
};

struct vpacket_s {
	/* all packets have a header */
	vpacket_header_t header;
	/* additional header for SOCK_STREAM packets */
	vpacket_tcp_header_t tcp_header;
	/* application data */
	guint16 data_size;
	gpointer payload;
};

struct vpacket_pod_s {
	enum vpacket_pod_flags pod_flags;
	struct vpacket_mgr_s* vp_mgr;
	vpacket_tp vpacket;
	GMutex* lock;
};

/* packet pods are wrapped by a reference counter */
typedef void (*rc_vpacket_pod_destructor_fp)(vpacket_pod_tp vpacket_pod);
struct rc_vpacket_pod_s {
	vpacket_pod_tp pod;
	gint8 reference_count;
	rc_vpacket_pod_destructor_fp destructor;
};

rc_vpacket_pod_tp rc_vpacket_pod_create(vpacket_pod_tp vp_pod, rc_vpacket_pod_destructor_fp destructor);
vpacket_pod_tp rc_vpacket_pod_get(rc_vpacket_pod_tp rc_vpacket_pod);
void rc_vpacket_pod_retain(rc_vpacket_pod_tp rc_vpacket_pod);
void rc_vpacket_pod_release(rc_vpacket_pod_tp rc_vpacket_pod);
#define rc_vpacket_pod_retain_stack(rc_vpacket_pod) rc_vpacket_pod_retain(rc_vpacket_pod)
#define rc_vpacket_pod_release_stack(rc_vpacket_pod) rc_vpacket_pod_release(rc_vpacket_pod)

vpacket_tp vpacket_set(vpacket_tp vpacket, guint8 protocol, in_addr_t src_addr, in_port_t src_port,
		in_addr_t dst_addr, in_port_t dst_port, enum vpacket_tcp_flags flags,
		guint32 seq_number, guint32 ack_number, guint32 advertised_window,
		guint16 data_size, const gpointer data);
guint32 vpacket_get_size(rc_vpacket_pod_tp rc_packet);
void vpacket_log(rc_vpacket_pod_tp vpacket_pod);

#endif /* VPACKET_H_ */
