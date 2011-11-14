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

#include <glib.h>
#include <stdlib.h>
#include <stdint.h>
#include <netinet/in.h>

#include "shadow.h"

vepoll_tp vepoll_create(vevent_mgr_tp vev_mgr, in_addr_t addr, guint16 sockd) {
	vepoll_tp vep = calloc(1, sizeof(vepoll_t));
	vep->addr = addr;
	vep->sockd = sockd;
	vep->vev_mgr = vev_mgr;
	vep->do_read_first = 1;

	/* the socket starts inactive */
	vep->state = VEPOLL_INACTIVE;

	/* startup our polling timer. this *shouldnt* be needed if we correctly watch our buffers... */
//	vep->flags |= VEPOLL_POLL_SCHEDULED;
//	worker_scheduleEvent(socketpolltimerexpired_new(vep), VEPOLL_POLL_DELAY, (GQuark) vep->addr);

	return vep;
}

void vepoll_destroy(vepoll_tp vep) {
	if(vep != NULL) {
		if((vep->flags & VEPOLL_NOTIFY_SCHEDULED) || (vep->flags & VEPOLL_POLL_SCHEDULED) || (vep->flags && VEPOLL_EXECUTING)){
			/* an event is currently scheduled, do not destroy yet */
			vep->flags |= VEPOLL_CANCEL_AND_DESTROY;
		} else {
			free(vep);
		}
	}
}

static void vepoll_activate(vepoll_tp vep, gboolean immediate) {
	if(vep != NULL) {
		/* check if we need to schedule a notification */
		if((vep->flags & VEPOLL_NOTIFY_SCHEDULED) == 0) {
			vep->flags |= VEPOLL_NOTIFY_SCHEDULED;
			SimulationTime delay = immediate ? 1 : SIMTIME_ONE_SECOND;
			worker_scheduleEvent((Event*)socketactivated_new(vep->sockd), delay, (GQuark) vep->addr);
		}
	}
}

void vepoll_mark_available(vepoll_tp vep, enum vepoll_type type) {
	type &= (VEPOLL_READ|VEPOLL_WRITE);

	if(vep != NULL) {
		/* turn it on and schedule as needed */
		vep->available |= type;
		vepoll_activate(vep, TRUE);
	} else {
		warning("vepoll was NULL when trying to mark type %u", type);
	}
}

void vepoll_mark_unavailable(vepoll_tp vep, enum vepoll_type type) {
	type &= (VEPOLL_READ|VEPOLL_WRITE);

	if(vep != NULL) {
		/* turn it off */
		vep->available &= ~type;
	} else {
		warning("vepoll was NULL when trying to unmark type %u", type);
	}
}

guint8 vepoll_query_available(vepoll_tp vep, enum vepoll_type type) {
	type &= (VEPOLL_READ|VEPOLL_WRITE);

	if(vep != NULL && type) {
		if(vep->available & type) {
			return 1;
		}
	}
	return 0;
}

void vepoll_mark_active(vepoll_tp vep) {
	if(vep != NULL) {
		vep->state = VEPOLL_ACTIVE;
		vepoll_activate(vep, TRUE);
	}
}

void vepoll_mark_inactive(vepoll_tp vep) {
	if(vep != NULL) {
		vep->state = VEPOLL_INACTIVE;
	}
}

void vepoll_vevent_add(vepoll_tp vep, enum vepoll_type type) {
	type &= (VEPOLL_READ|VEPOLL_WRITE);

	if(vep != NULL) {
		vep->polling |= type;

		if(type & VEPOLL_READ) {
			vep->num_read++;
		}
		if(type & VEPOLL_WRITE) {
			vep->num_write++;
		}

		vepoll_activate(vep, TRUE);
	}
}

void vepoll_vevent_delete(vepoll_tp vep, enum vepoll_type type) {
	type &= (VEPOLL_READ|VEPOLL_WRITE);

	if(vep != NULL) {
		vep->polling &= ~type;

		if(type & VEPOLL_READ) {
			vep->num_read--;
		}
		if(type & VEPOLL_WRITE) {
			vep->num_write--;
		}
	}
}

void vepoll_execute_notification(vepoll_tp vep) {
	if(vep != NULL) {
		debug("activation for socket %u, can_write=%i, can_read=%i",
				vep->sockd, (vep->available & VEPOLL_WRITE) > 0, (vep->available & VEPOLL_READ) > 0);
#ifdef DEBUG
		vevent_mgr_print_stat(vep->vev_mgr, vep->sockd);
#endif

		/* the event is no longer scheduled */
		vep->flags &= ~VEPOLL_NOTIFY_SCHEDULED;

		/* check if we should follow through with the notification */
		if(vep->flags & VEPOLL_CANCEL_AND_DESTROY) {
			vepoll_destroy(vep);
			return;
		}

		/* are we allowed to tell the plugin */
		if(vep->state != VEPOLL_INACTIVE) {
			vep->flags |= VEPOLL_EXECUTING;

			guint8 turn = vep->do_read_first;

			Worker* worker = worker_getPrivate();
			Application* a = worker->cached_node->application;
			Plugin* plugin = worker_getPlugin(a->software);

			/* tell the socket about it if available, only switching context once */
			if((vep->available & VEPOLL_READ) && (vep->available & VEPOLL_WRITE)) {
				if(vep->do_read_first) {
					plugin_executeReadableWritable(plugin, a->state, vep->sockd);
				} else {
					plugin_executeWritableReadable(plugin, a->state, vep->sockd);
				}

				/* next time its the other types turn to go first */
				vep->do_read_first = vep->do_read_first == 1 ? 0 : 1;
			} else if(vep->available & VEPOLL_READ) {
				plugin_executeReadable(plugin, a->state, vep->sockd);
			} else if(vep->available & VEPOLL_WRITE) {
				plugin_executeWritable(plugin, a->state, vep->sockd);
			}

			/* tell vevent to execute its callbacks for this socket */
			if(turn) {
				if(vep->available & VEPOLL_READ) {
					vevent_mgr_notify_can_read(vep->vev_mgr, vep->sockd);
				}
				if(vep->available & VEPOLL_WRITE) {
					vevent_mgr_notify_can_write(vep->vev_mgr, vep->sockd);
				}
			} else {
				if(vep->available & VEPOLL_WRITE) {
					vevent_mgr_notify_can_write(vep->vev_mgr, vep->sockd);
				}
				if(vep->available & VEPOLL_READ) {
					vevent_mgr_notify_can_read(vep->vev_mgr, vep->sockd);
				}
			}

			/* if vevent is still waiting for more, reactivate it */
			if(((vep->num_read > 0) && (vep->available & VEPOLL_READ)) ||
				((vep->num_write > 0) && (vep->available & VEPOLL_WRITE))) {
				vepoll_activate(vep, FALSE);
			}

			vep->flags &= ~VEPOLL_EXECUTING;
			if(vep->flags & VEPOLL_CANCEL_AND_DESTROY) {
				vepoll_destroy(vep);
				return;
			}
		} else {
			debug("canceling notification for inactive socket sd %u", vep->sockd);
		}
	}
}

/* make sure sockets don't get stuck */
void vepoll_poll(vepoll_tp vep, vsocket_mgr_tp vs_mgr) {
	if(!vep) {
		return;
	}
    g_assert(vs_mgr);

	/* poll no longer scheduled */
	vep->flags &= ~VEPOLL_POLL_SCHEDULED;

	if(vep->flags & VEPOLL_CANCEL_AND_DESTROY) {
		vepoll_destroy(vep);
		return;
	}

	/* TODO move this out of vepoll and to a higher level */
#ifdef DEBUG
	vsocket_mgr_print_stat(vs_mgr, vep->sockd);
	vevent_mgr_print_stat(vep->vev_mgr, vep->sockd);
#endif

	vepoll_activate(vep, TRUE);

	vep->flags |= VEPOLL_POLL_SCHEDULED;
	worker_scheduleEvent((Event*)socketpolltimerexpired_new(vep), VEPOLL_POLL_DELAY, (GQuark) vep->addr);
}
