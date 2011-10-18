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

#ifndef VTCP_H_
#define VTCP_H_

#include <glib.h>
#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

#include "shadow.h"

/* -maximum size data we can send network:
 *	-tcp truncates and only sends 65536
 */
#define VTRANSPORT_TCP_MAX_STREAM_SIZE 65535
/* the delayed ack timer in milliseconds */
#define VTRANSPORT_TCP_DACK_TIMER 10*SIMTIME_ONE_MILLISECOND
/* initial sequence number */
#define VTRANSPORT_TCP_ISS 0

enum vtcp_delayed_ack {
	dack_scheduled = 1, dack_requested = 2
};

struct vtcp_s {
	vsocket_mgr_tp vsocket_mgr;
	vsocket_tp sock;
	vbuffer_tp vb;
	vpeer_tp remote_peer;
	/* set if the connection was destroyed because it was reset */
	guint8 connection_was_reset;
	/* acks are delayed to get a chance to piggyback on data */
	enum vtcp_delayed_ack snd_dack;
	/* used to make sure we get all data when other end closes */
	guint32 rcv_end;
	/* the last byte that was sent by the app, possibly not yet sent to the network */
	guint32 snd_end;
	/* send unacknowledged */
	guint32 snd_una;
	/* send next */
	guint32 snd_nxt;
	/* send window */
	guint32 snd_wnd;
	/* send sequence number used for last window update */
	guint32 snd_wl1;
	/* send ack number used from last window update */
	guint32 snd_wl2;
	/* receive next */
	guint32 rcv_nxt;
	/* receive window */
	guint32 rcv_wnd;
	/* initial receive sequence number */
	guint32 rcv_irs;
	/* congestion control, used for AIMD and slow start */
	guint8 is_slow_start;
	guint32 cng_wnd;
	guint32 cng_threshold;
	guint32 last_adv_wnd;
};

void vtcp_connect(vtcp_tp vtcp, in_addr_t remote_addr, in_port_t remote_port);
vtcp_tp vtcp_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, vbuffer_tp vb);
rc_vpacket_pod_tp vtcp_create_packet(vtcp_tp vtcp, enum vpacket_tcp_flags flags, guint16 data_size, const gpointer data);
void vtcp_destroy(vtcp_tp vtcp);
void vtcp_disconnect(vtcp_tp vtcp);
guint32 vtcp_generate_iss();
vsocket_tp vtcp_get_target_socket(vtransport_item_tp titem);
void vtcp_checkdack(vtcp_tp vtcp);
enum vt_prc_result vtcp_process_item(vtransport_item_tp titem);
ssize_t vtcp_recv(vsocket_mgr_tp net, vsocket_tp tcpsock, gpointer dest_buf, size_t n);
void vtcp_retransmit(vtcp_tp vtcp, guint32 retransmit_key);
ssize_t vtcp_send(vsocket_mgr_tp net, vsocket_tp tcpsock, const gpointer src_buf, size_t n);
void vtcp_send_control_packet(vtcp_tp vtcp, enum vpacket_tcp_flags flags);
guint8 vtcp_send_packet(vtcp_tp vtcp, rc_vpacket_pod_tp rc_packet);
rc_vpacket_pod_tp vtcp_wire_packet(vtcp_tp vtcp);

#endif /* VTCP_H_ */
