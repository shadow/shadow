/*
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

#ifndef VBUFFER_H_
#define VBUFFER_H_

#include <glib.h>
#include <stdint.h>
#include <stddef.h>

#include "shadow.h"

struct vbuffer_sbuf_s {
	/* rc_packets to send, keyed by position in sliding window (flow/congestion control) */
	orderedlist_tp vwrite;
	/* packets that can be sent immediately, have no data */
	GQueue *tcp_control;
	/* rc_packets sent but not acked */
	orderedlist_tp tcp_retransmit;
	guint64 max_size;
	guint64 current_size;
	guint16 num_packets;
};

struct vbuffer_rbuf_s {
	/* rc_packets with user data */
	GQueue *vread;
	/* rc_packets waiting for a gap to be filled for in-order processing */
	orderedlist_tp tcp_unprocessed;
	/* users read offset ginto the packet at the front of the data list */
	guint16 data_offset;
	guint64 max_size;
	guint64 current_size;
	guint16 num_packets;
};

struct vbuffer_s {
	vepoll_tp vep;
	vbuffer_rbuf_tp rbuf;
	vbuffer_sbuf_tp sbuf;
};

gint vbuffer_get_send_length(vbuffer_tp vb);
void vbuffer_set_size(vbuffer_tp vb, guint64 rbuf_max, guint64 sbuf_max);
size_t vbuffer_receive_space_available(vbuffer_tp vb);
size_t vbuffer_send_space_available(vbuffer_tp vb);
void vbuffer_clear_tcp_retransmit(vbuffer_tp vb, guint8 only_clear_acked, guint32 acknum);
void vbuffer_clear_send(vbuffer_tp vb);
vbuffer_tp vbuffer_create(guint8 type, guint64 max_recv_space, guint64 max_send_space, vepoll_tp vep);
void vbuffer_destroy(vbuffer_tp vb);
rc_vpacket_pod_tp vbuffer_get_read(vbuffer_tp vb, guint16** read_offset);
guint8 vbuffer_is_empty(vbuffer_tp vb);
guint8 vbuffer_is_empty_send_control(vbuffer_tp vb);
guint8 vbuffer_is_readable(vbuffer_tp vb);
guint8 vbuffer_is_writable(vbuffer_tp vb);

guint8 vbuffer_add_receive(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet);
guint8 vbuffer_add_read(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet);
guint8 vbuffer_add_send(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, guint32 transmit_key);
guint8 vbuffer_add_retransmit(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet, guint32 retransmit_key);
guint8 vbuffer_add_control(vbuffer_tp vb, rc_vpacket_pod_tp rc_packet);

rc_vpacket_pod_tp vbuffer_get_read(vbuffer_tp vb, guint16** read_offset);
rc_vpacket_pod_tp vbuffer_remove_read(vbuffer_tp vb);
rc_vpacket_pod_tp vbuffer_get_tcp_unprocessed(vbuffer_tp vb, guint32 next_sequence);
rc_vpacket_pod_tp vbuffer_remove_tcp_unprocessed(vbuffer_tp vb, guint32 next_sequence);
rc_vpacket_pod_tp vbuffer_get_send(vbuffer_tp vb);
rc_vpacket_pod_tp vbuffer_remove_send(vbuffer_tp vb, guint32 transmit_key);
rc_vpacket_pod_tp vbuffer_remove_tcp_retransmit(vbuffer_tp vb, guint32 retransmit_key);
rc_vpacket_pod_tp vbuffer_remove_tcp_control(vbuffer_tp vb);

#endif /* VBUFFER_H_ */
