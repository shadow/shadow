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

#ifndef VTRANSPORT_MGR_H_
#define VTRANSPORT_MGR_H_

#include <glib.h>
#include <stdint.h>

#include "shadow.h"

/* we will batch packet transfers until we consume this many nanoseconds of bandwidth */
#define VTRANSPORT_NS_PER_MS 1000000
#define VTRANSPORT_MGR_BATCH_TIME 10 * VTRANSPORT_NS_PER_MS /* = 10 milliseconds */

#define CPU_LOAD_MULTIPLIER 0

struct vtransport_mgr_inq_s {
	/* rc_packets coming ginto this node from the wire */
	GQueue *buffer;
	/* burst rate of incoming packets */
	guint64 max_size;
	guint64 current_size;
};

struct vtransport_mgr_s {
	vsocket_mgr_tp vsocket_mgr;
	/* nanos to receive a single byte */
	gdouble nanos_per_byte_down;
	/* nanos to send a single byte */
	gdouble nanos_per_byte_up;
	guint32 KBps_down;
	guint32 KBps_up;
	/* list<vsocket_tp>, list of sockets that have packets waiting to be sent */
	GQueue *ready_to_send;
	/* set if an incoming packet can trigger a send event */
	guint8 ok_to_fire_send;
	/* list<rc_packet>, essentially the NIC queue - packets waiting to be received */
	vtransport_mgr_inq_tp inq;
	/* set if an incoming packet can trigger a recv event */
	guint8 ok_to_fire_recv;
	SimulationTime last_time_sent;
	SimulationTime last_time_recv;
	SimulationTime nanos_consumed_sent;
	SimulationTime nanos_consumed_recv;
};

vtransport_mgr_tp vtransport_mgr_create(vsocket_mgr_tp vsocket_mgr, guint32 KBps_down, guint32 KBps_up);
void vtransport_mgr_destroy(vtransport_mgr_tp vt_mgr);
void vtransport_mgr_download_next(vtransport_mgr_tp vt_mgr);
void vtransport_mgr_ready_receive(vtransport_mgr_tp vt_mgr, vsocket_tp sock, rc_vpacket_pod_tp rc_packet);
void vtransport_mgr_ready_send(vtransport_mgr_tp vt_mgr, vsocket_tp sock);
void vtransport_mgr_upload_next(vtransport_mgr_tp vt_mgr);

#endif /* VTRANSPORT_MGR_H_ */
