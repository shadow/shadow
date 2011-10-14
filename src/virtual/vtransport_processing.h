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

#ifndef VTRANSPORT_PROCESSING_H_
#define VTRANSPORT_PROCESSING_H_

#include <glib.h>

#include "shadow.h"

enum vt_prc_result {
	VT_PRC_NONE=0, VT_PRC_DROPPED=1, VT_PRC_READABLE=2, VT_PRC_WRITABLE=4, VT_PRC_PARENT_READABLE=8, VT_PRC_SENDABLE=16, VT_PRC_DESTROY=32, VT_PRC_RESET=64
};

struct vtransport_item_s {
	rc_vpacket_pod_tp rc_packet;
	guint16 sockd;
	vsocket_tp sock;
};

#endif /* VTRANSPORT_PROCESSING_H_ */
