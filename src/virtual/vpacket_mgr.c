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

#include <glib.h>
#include <stdlib.h>
#include <stdint.h>
#include <strings.h>

#include "shadow.h"

vpacket_mgr_tp vpacket_mgr_create() {
	vpacket_mgr_tp vp_mgr = malloc(sizeof(vpacket_mgr_t));

	vp_mgr->lock_regular_packets = 0;

	return vp_mgr;
}

void vpacket_mgr_destroy(vpacket_mgr_tp vp_mgr) {
	if(vp_mgr != NULL) {
		free(vp_mgr);
	}
}

rc_vpacket_pod_tp vpacket_mgr_packet_create(vpacket_mgr_tp vp_mgr, guint8 protocol,
		in_addr_t src_addr, in_port_t src_port, in_addr_t dst_addr, in_port_t dst_port,
		enum vpacket_tcp_flags flags, guint32 seq_number, guint32 ack_number, guint32 advertised_window,
		guint16 data_size, const gpointer data) {
	/* get vpod memory */
	vpacket_pod_tp vp_pod = g_malloc0(sizeof(vpacket_pod_t));
	vp_pod->vp_mgr = vp_mgr;
	vp_pod->pod_flags = VP_OWNED;

	vp_pod->lock = g_mutex_new();

	vp_pod->vpacket = g_malloc0(sizeof(vpacket_t));

	if(data_size > 0) {
		vp_pod->vpacket->payload = g_malloc(data_size);
	} else {
		vp_pod->vpacket->payload = NULL;
	}

	rc_vpacket_pod_tp rc_vpacket = rc_vpacket_pod_create(vp_pod, &vpacket_mgr_vpacket_pod_destructor_cb);

	/* dont bother locking, since no one else has access yet */
	if(rc_vpacket != NULL && rc_vpacket->pod != NULL) {
		/* set contents */
		vpacket_set(rc_vpacket->pod->vpacket, protocol, src_addr, src_port, dst_addr, dst_port,
				flags, seq_number, ack_number, advertised_window, data_size, data);
	}

	return rc_vpacket;
}

void vpacket_mgr_vpacket_pod_destructor_cb(vpacket_pod_tp vp_pod) {
	if(vp_pod != NULL) {
		if(vp_pod->vpacket != NULL) {
			if(vp_pod->vpacket->payload != NULL) {
				free(vp_pod->vpacket->payload);
			}
			free(vp_pod->vpacket);
		}

		if(vp_pod->lock != NULL) {
			g_mutex_free(vp_pod->lock);
		}

		free(vp_pod);
	} else {
		warning("vpacket_pod_rc_destructor_cb: unable to destroy NULL pod\n");
	}
}

vpacket_tp vpacket_mgr_lockcontrol(rc_vpacket_pod_tp rc_vp_pod, enum vpacket_lockcontrol command) {
	vpacket_pod_tp vp_pod = rc_vpacket_pod_get(rc_vp_pod);

	enum vpacket_lockcontrol operation = command & (LC_OP_READLOCK | LC_OP_READUNLOCK | LC_OP_WRITELOCK | LC_OP_WRITEUNLOCK);
//	enum vpacket_lockcontrol target = command & (LC_TARGET_PACKET | LC_TARGET_PAYLOAD);

	if(vp_pod != NULL) {
		vpacket_mgr_tp vp_mgr = vp_pod->vp_mgr;
		if(vp_mgr != NULL && vp_mgr->lock_regular_packets && vp_pod->vpacket != NULL) {
			switch (operation) {
				case LC_OP_READLOCK:
				case LC_OP_WRITELOCK: {
					g_mutex_lock(vp_pod->lock);
					break;
				}
				case LC_OP_READUNLOCK:
				case LC_OP_WRITEUNLOCK: {
					g_mutex_unlock(vp_pod->lock);
					break;
				}
				default: {
					warning("vpacket_mgr_lockcontrol: undefined command\n");
					break;
				}
			}
		} else {
			/* no locking */
			return vp_pod->vpacket;
		}
	}
	return NULL;
}
