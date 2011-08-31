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

#ifndef VTCP_SERVER_H_
#define VTCP_SERVER_H_

#include <glib.h>
#include <stdint.h>
#include <glib-2.0/glib.h>

#include "vsocket_mgr.h"
#include "vsocket.h"

typedef struct vtcp_server_child_s {
	/* TODO should we store the sock_descrs instead of poginters to sockets?
	 * then we would do a lookup on the fly from vsocket_mgr, preventing a dangling poginter when the socket is deleted. */
	vsocket_tp sock;
	guint key;
} vtcp_server_child_t, *vtcp_server_child_tp;

typedef struct vtcp_server_s {
	vsocket_mgr_tp vsocket_mgr;
	vsocket_tp sock;
	/* vtcp_server_child_tp of requested connections that are not yet established, max size of this
	 * list is VSOCKET_MAX_SYN_BACKLOG = 1024
	 * keyed by hash(remoteaddr, remoteport) */
	GHashTable *incomplete_children;
	/* maximum length of pending connections (will be capped at SOMAXCONN = 128) */
	guint8 backlog;
	/* vtcp_server_child_tp of completely established connections waiting to be accepted
	 * keyed by hash(remoteaddr, remoteport) */
	GHashTable *pending_children;
	/* pending vtcp_server_child_tp are also stored in a queue so we accept them in order. */
	GQueue *pending_queue;
	/* vtcp_server_child_tp of established connections that have been accepted
	 * keyed by hash(remoteaddr, remoteport) */
	GHashTable *accepted_children;
}vtcp_server_t, *vtcp_server_tp;

void vtcp_server_add_child_accepted(vtcp_server_tp server, vtcp_server_child_tp schild);
void vtcp_server_add_child_incomplete(vtcp_server_tp server, vtcp_server_child_tp schild);
guint8 vtcp_server_add_child_pending(vtcp_server_tp server, vtcp_server_child_tp schild);
vtcp_server_tp vtcp_server_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, gint backlog);
vtcp_server_child_tp vtcp_server_create_child(vtcp_server_tp server, in_addr_t remote_addr, in_port_t remote_port);
void vtcp_server_destroy(vtcp_server_tp server);
void vtcp_server_destroy_cb(gint key, gpointer value, gpointer param);
void vtcp_server_destroy_child(vtcp_server_tp server, vtcp_server_child_tp schild);
vtcp_server_child_tp vtcp_server_get_child(vtcp_server_tp server, in_addr_t remote_addr, in_port_t remote_port);
guint8 vtcp_server_is_empty(vtcp_server_tp server);
void vtcp_server_remove_child_accepted(vtcp_server_tp server, vtcp_server_child_tp schild);
void vtcp_server_remove_child_incomplete(vtcp_server_tp server, vtcp_server_child_tp schild);
vtcp_server_child_tp vtcp_server_remove_child_pending(vtcp_server_tp server);

#endif /* VTCP_SERVER_H_ */
