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

#include "shadow.h"

EventFunctionTable packetsent_functions = {
	(EventRunFunc) packetsent_run,
	(EventFreeFunc) packetsent_free,
	MAGIC_VALUE
};

PacketSentEvent* packetsent_new() {
	PacketSentEvent* event = g_new0(PacketSentEvent, 1);
	MAGIC_INIT(event);

	shadowevent_init(&(event->super), &packetsent_functions);

	return event;
}

void packetsent_run(PacketSentEvent* event, Node* node) {
	MAGIC_ASSERT(event);
	MAGIC_ASSERT(node);

	vtransport_mgr_upload_next(node->vsocket_mgr->vt_mgr);
}

void packetsent_free(PacketSentEvent* event) {
	MAGIC_ASSERT(event);
	MAGIC_CLEAR(event);
	g_free(event);
}
