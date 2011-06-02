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

#ifndef _vci_event_h
#define _vci_event_h

#include "global.h"

#ifndef VEPOLL_H_
#include "vsocket_mgr.h"
#endif

enum vci_event_code {
	VCI_EC_ONPACKET,
	VCI_EC_ONNOTIFY,
	VCI_EC_ONPOLL,
	VCI_EC_ONDACK,
	VCI_EC_ONUPLOADED,
	VCI_EC_ONDOWNLOADED,
	VCI_EC_ONRETRANSMIT,
	VCI_EC_ONCLOSE
};

typedef struct vci_event_vtable_s {
        void *exec_cb;
        void *destroy_cb;
        void *deposit_cb;
} vci_event_vtable_t, *vci_event_vtable_tp;

typedef struct vci_event_s {
	enum vci_event_code code;
	ptime_t deliver_time;
	in_addr_t node_addr;
	in_addr_t owner_addr;
	uint64_t cpu_delay_position;
	void *payload;
        int free_payload;
        vci_event_vtable_tp vtable;
} vci_event_t, *vci_event_tp;


//typedef void (*vci_exec_func)(vci_event_tp vci_event, vsocket_mgr_tp vs_mgr);

#endif
