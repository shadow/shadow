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

#ifndef VEPOLL_H_
#define VEPOLL_H_

#include <stdint.h>
#include <netinet/in.h>

#include "vevent_mgr.h"
#include "context.h"
#include "vci_event.h"

/* vepoll monitors the state of io events on a socket, and manages scheduling
 * events for notifying the modules that the socket is readable/writable. we
 * only allow a single event to be scheduled for for each type to prevent
 * unnecessary callbacks. because of this, we do NOT need to keep counters
 * of how many outstanding events we created.
 */

#define VEPOLL_POLL_DELAY 1000

enum vepoll_type {
	VEPOLL_READ=1, /* the socket can be read, i.e. there is data waiting for user */
	VEPOLL_WRITE=2, /* the socket can be written, i.e. there is available buffer space */
};

enum vepoll_state {
	VEPOLL_INACTIVE, /* never notify user (b/c they e.g. closed the socket or did not accept yet) */
	VEPOLL_ACTIVE, /* ok to notify user as far as we know, socket is ready */
};

enum vepoll_flags {
	VEPOLL_NOTIFY_SCHEDULED=1, /* we scheduled a callback with shadow to notify user */
	VEPOLL_POLL_SCHEDULED=2, /* we scheduled a poll callback with shadow */
	VEPOLL_CANCEL_AND_DESTROY=4, /* we should cancel the callback and destroy vepoll */
	VEPOLL_EXECUTING=8,
};

typedef struct vepoll_s {
	in_addr_t addr;
	uint16_t sockd;
	/* OR'ed with types that are allowed (i.e. readable/writable) */
	enum vepoll_type available;
	/* OR'ed with types that vevent is waiting for (i.e. readable/writable) */
	enum vepoll_type polling;
	uint16_t num_read;
	uint16_t num_write;
	/* my current state */
	enum vepoll_state state;
	/* OR'ed with various flags we are interested in */
	enum vepoll_flags flags;
	vevent_mgr_tp vev_mgr;
	uint8_t do_read_first;
} vepoll_t, *vepoll_tp;

vepoll_tp vepoll_create(vevent_mgr_tp vev_mgr, in_addr_t addr, uint16_t sockd);
void vepoll_destroy(vepoll_tp vep);

/* the socket is active and can be notified when available */
void vepoll_mark_active(vepoll_tp vep);
/* the socket is inactive and can be notified when available */
void vepoll_mark_inactive(vepoll_tp vep);
/* mark the socket as available for type, notify module if needed
 * returns 0 on success, -1 on error */
void vepoll_mark_available(vepoll_tp vep, enum vepoll_type type);
/* mark the socket as unavailable for type
 * returns 0 on success, -1 on error */
void vepoll_mark_unavailable(vepoll_tp vep, enum vepoll_type type);
/* returns 1 if the socket is available for type, 0 othewise */
uint8_t vepoll_query_available(vepoll_tp vep, enum vepoll_type type);

/* vevent wants to be notified when status changes for this sock/pipe */
void vepoll_vevent_add(vepoll_tp vep, enum vepoll_type type);
/* vevent no longer wants to be notified when status changes for this sock/pipe */
void vepoll_vevent_delete(vepoll_tp vep, enum vepoll_type type);

/* scheduler popped our event, so we should notify module of socket is ready */
void vepoll_execute_notification(context_provider_tp provider, vepoll_tp vep);
/* called every polling interval to check status and activate as needed */
void vepoll_onpoll(vci_event_tp vci_event, void *vs_mgr);

#endif /* VEPOLL_H_ */
