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

#ifndef VTCP_H_
#define VTCP_H_

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

#include "vsocket_mgr.h"
#include "vsocket.h"
#include "vtransport_processing.h"
#include "vpeer.h"
#include "vbuffer.h"
#include "orderedlist.h"
#include "vci_event.h"

/* -maximum size data we can send network:
 *	-tcp truncates and only sends 65536
 */
#define VTRANSPORT_TCP_MAX_STREAM_SIZE 65535
/* the delayed ack timer in milliseconds */
#define VTRANSPORT_TCP_DACK_TIMER 10
/* initial sequence number */
#define VTRANSPORT_TCP_ISS 0

enum vtcp_delayed_ack {
	dack_scheduled = 1, dack_requested = 2
};

typedef struct vtcp_s {
	vsocket_mgr_tp vsocket_mgr;
	vsocket_tp sock;
	vbuffer_tp vb;
	vpeer_tp remote_peer;
	/* set if the connection was destroyed because it was reset */
	uint8_t connection_was_reset;
	/* acks are delayed to get a chance to piggyback on data */
	enum vtcp_delayed_ack snd_dack;
	/* used to make sure we get all data when other end closes */
	uint32_t rcv_end;
	/* the last byte that was sent by the app, possibly not yet sent to the network */
	uint32_t snd_end;
	/* send unacknowledged */
	uint32_t snd_una;
	/* send next */
	uint32_t snd_nxt;
	/* send window */
	uint32_t snd_wnd;
	/* send sequence number used for last window update */
	uint32_t snd_wl1;
	/* send ack number used from last window update */
	uint32_t snd_wl2;
	/* receive next */
	uint32_t rcv_nxt;
	/* receive window */
	uint32_t rcv_wnd;
	/* initial receive sequence number */
	uint32_t rcv_irs;
	/* congestion control, used for AIMD and slow start */
	uint8_t is_slow_start;
	uint32_t cng_wnd;
	uint32_t cng_threshold;
	uint32_t last_adv_wnd;
}vtcp_t, *vtcp_tp;

void vtcp_connect(vtcp_tp vtcp, in_addr_t remote_addr, in_port_t remote_port);
vtcp_tp vtcp_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, vbuffer_tp vb);
rc_vpacket_pod_tp vtcp_create_packet(vtcp_tp vtcp, enum vpacket_tcp_flags flags, uint16_t data_size, const void* data);
void vtcp_destroy(vtcp_tp vtcp);
void vtcp_disconnect(vtcp_tp vtcp);
uint32_t vtcp_generate_iss();
vsocket_tp vtcp_get_target_socket(vtransport_item_tp titem);
void vtcp_ondack(vci_event_tp vci_event, vsocket_mgr_tp vs_mgr);
enum vt_prc_result vtcp_process_item(vtransport_item_tp titem);
ssize_t vtcp_recv(vsocket_mgr_tp net, vsocket_tp tcpsock, void* dest_buf, size_t n);
void vtcp_retransmit(vtcp_tp vtcp, uint32_t retransmit_key);
ssize_t vtcp_send(vsocket_mgr_tp net, vsocket_tp tcpsock, const void* src_buf, size_t n);
void vtcp_send_control_packet(vtcp_tp vtcp, enum vpacket_tcp_flags flags);
uint8_t vtcp_send_packet(vtcp_tp vtcp, rc_vpacket_pod_tp rc_packet);
rc_vpacket_pod_tp vtcp_wire_packet(vtcp_tp vtcp);

#endif /* VTCP_H_ */
