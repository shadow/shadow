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

#include "shadow.h"

EventFunctionTable packetarrived_functions = {
	(EventRunFunc) packetarrived_run,
	(EventFreeFunc) packetarrived_free,
	MAGIC_VALUE
};

PacketArrivedEvent* packetarrived_new(rc_vpacket_pod_tp rc_packet) {
	PacketArrivedEvent* event = g_new0(PacketArrivedEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &packetarrived_functions);
	event->rc_packet = rc_packet;
	rc_vpacket_pod_retain(rc_packet);

	return event;
}

void packetarrived_run(PacketArrivedEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

    rc_vpacket_pod_tp rc_packet = event->rc_packet;
    if(rc_packet != NULL) {
        vpacket_log_debug(rc_packet);

        rc_vpacket_pod_retain_stack(rc_packet);

        /* called when there is an incoming packet. */
        debug("event fired");

        if(node->vsocket_mgr->vt_mgr != NULL) {
            vsocket_tp sock = vsocket_mgr_get_socket_receiver(node->vsocket_mgr, rc_packet);
            if(sock != NULL) {
                vtransport_mgr_ready_receive(node->vsocket_mgr->vt_mgr, sock, rc_packet);
            } else {
                debug("socket no longer exists, dropping packet");
            }
        }

        debug("releasing stack");
        rc_vpacket_pod_release_stack(rc_packet);
    }
}

void packetarrived_free(PacketArrivedEvent* event) {
	MAGIC_ASSERT(event);
	rc_vpacket_pod_release(event->rc_packet);
	MAGIC_CLEAR(event);
	g_free(event);
}
