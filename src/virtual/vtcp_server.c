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
#include <string.h>

#include "shadow.h"

static void vtcp_server_add_child_helper(GHashTable *ht, vtcp_server_child_tp schild);

vtcp_server_tp vtcp_server_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, gint backlog) {
	vtcp_server_tp server = malloc(sizeof(vtcp_server_t));

	/* silently truncate backlog at our max level of SOMAXCONN */
//	turn this off for now - we are unable to start many nodes at once otherwise
//	if(backlog > 0 && backlog < SOMAXCONN){
//		server->backlog = (guint8) backlog;
//	} else {
//		server->backlog = SOMAXCONN;
//	}

	server->vsocket_mgr = vsocket_mgr;
	server->sock = sock;

	server->incomplete_children = g_hash_table_new(g_int_hash, g_int_equal);
	server->pending_children = g_hash_table_new(g_int_hash, g_int_equal);
	server->pending_queue = g_queue_new();
	server->accepted_children = g_hash_table_new(g_int_hash, g_int_equal);

	return server;
}

void vtcp_server_destroy_cb(gpointer key, gpointer value, gpointer param) {
	vtcp_server_destroy((vtcp_server_tp) value);
}

void vtcp_server_destroy(vtcp_server_tp server) {
	if(server != NULL){
//		server->backlog = 0;

		g_hash_table_foreach(server->incomplete_children, vsocket_mgr_destroy_and_remove_socket_cb, server->vsocket_mgr);
		g_hash_table_foreach(server->pending_children, vsocket_mgr_destroy_and_remove_socket_cb, server->vsocket_mgr);
		g_hash_table_foreach(server->accepted_children, vsocket_mgr_destroy_and_remove_socket_cb, server->vsocket_mgr);

		g_hash_table_destroy(server->incomplete_children);
		g_hash_table_destroy(server->pending_children);
		g_hash_table_destroy(server->accepted_children);

		/* vsockets stored in pending queue were just deleted from hashtable */
		g_queue_free(server->pending_queue);

		free(server);
	}
}

guint8 vtcp_server_is_empty(vtcp_server_tp server) {
	if(server != NULL && 
           (g_hash_table_size(server->accepted_children) > 0 ||
            g_hash_table_size(server->incomplete_children) > 0 ||
            g_hash_table_size(server->incomplete_children) > 0)) {
		return 0;
	}
        return 1;
}

vtcp_server_child_tp vtcp_server_create_child(vtcp_server_tp server, in_addr_t remote_addr, in_port_t remote_port) {
	vtcp_server_child_tp schild = malloc(sizeof(vtcp_server_child_t));

	schild->key = vsocket_hash(remote_addr, remote_port);
	schild->sock = vsocket_mgr_create_socket(server->vsocket_mgr, SOCK_STREAM);
	vsocket_mgr_add_socket(server->vsocket_mgr, schild->sock);

	/* not active till accepted */
	schild->sock->is_active = 0;

	/* new socket will be bound to its own port */
	struct sockaddr_in newaddr;
	if(remote_addr == htonl(INADDR_LOOPBACK)) {
		newaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	} else {
		newaddr.sin_addr.s_addr = server->vsocket_mgr->addr;
	}
	newaddr.sin_port = htons(server->vsocket_mgr->next_rnd_port++);
	newaddr.sin_family = PF_UNIX;

	gint result = vsocket_bind(server->vsocket_mgr, schild->sock->sock_desc, &newaddr, sizeof(newaddr));

	/* if there was an error in bind, cleanup mapping added in socket() */
	if(result == VSOCKET_ERROR){
		warning("unable to create new connection as requested");
		vsocket_mgr_destroy_and_remove_socket(server->vsocket_mgr, schild->sock);
		return NULL;
	}

	/* attach it to connection, dont call connect as that will start new handshake */
	schild->sock->sock_desc_parent = server->sock->sock_desc;

	debug("creating multiplexed socket sd %u for server sd %u",
			schild->sock->sock_desc, schild->sock->sock_desc_parent);

	return schild;
}

void vtcp_server_destroy_child(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL && schild != NULL && schild->sock != NULL){
		debug("destroying multiplexed socket sd %u for server sd %u",
					schild->sock->sock_desc, schild->sock->sock_desc_parent);

		/* remove all possible links to child */
		g_hash_table_remove(server->incomplete_children, &(schild->key));
		g_hash_table_remove(server->pending_children, &(schild->key));
		g_hash_table_remove(server->accepted_children, &(schild->key));

		/* TODO do something smarter instead of re-creating the
		 * pending queue... like a list iterator */
		if(g_queue_get_length(server->pending_queue) > 0) {
			GQueue *new_pending = g_queue_new();
			while(g_queue_get_length(server->pending_queue) > 0) {
				vsocket_tp next = g_queue_pop_head(server->pending_queue);
				if(next->sock_desc != schild->sock->sock_desc) {
					g_queue_push_tail(new_pending, next);
				}
			}
			g_queue_free(server->pending_queue);
			server->pending_queue = new_pending;
		}
	}
	if(schild) {
		memset(schild, 0, sizeof(vtcp_server_child_t));
		free(schild);
	}
}

vtcp_server_child_tp vtcp_server_get_child(vtcp_server_tp server, in_addr_t remote_addr, in_port_t remote_port) {
	if(server != NULL) {
		guint hashkey = vsocket_hash(remote_addr, remote_port);
        gconstpointer key = &(hashkey);

		vtcp_server_child_tp target = NULL;

		/* look through existing connections */
		target = g_hash_table_lookup(server->accepted_children, key);
		if(target == NULL){
			target = g_hash_table_lookup(server->incomplete_children, key);
			if(target == NULL){
				target = g_hash_table_lookup(server->pending_children, key);
			}
		}

		return target;
	} else {
		return NULL;
	}
}

void vtcp_server_add_child_incomplete(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL) {
		vtcp_server_add_child_helper(server->incomplete_children, schild);
	}
}

void vtcp_server_remove_child_incomplete(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL && schild != NULL) {
		g_hash_table_remove(server->incomplete_children, &(schild->key));
	}
}

guint8 vtcp_server_add_child_pending(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL) {
//		Disabled backlog for now
//		if(g_queue_get_length(server->pending_queue) < server->backlog) {
			vtcp_server_add_child_helper(server->pending_children, schild);
			g_queue_push_tail(server->pending_queue, schild);
			return 1;
//		}
	}
	return 0;
}

vtcp_server_child_tp vtcp_server_remove_child_pending(vtcp_server_tp server) {
	if(server != NULL && server->pending_queue != NULL) {
		vtcp_server_child_tp pending = g_queue_pop_head(server->pending_queue);
		if(pending != NULL) {
			g_hash_table_remove(server->pending_children, &(pending->key));
		}
		return pending;
	} else {
		return NULL;
	}
}

void vtcp_server_add_child_accepted(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL) {
		vtcp_server_add_child_helper(server->accepted_children, schild);
	}
}

void vtcp_server_remove_child_accepted(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL && schild != NULL) {
		g_hash_table_remove(server->accepted_children, &(schild->key));
	}
}

static void vtcp_server_add_child_helper(GHashTable *ht, vtcp_server_child_tp schild) {
	if(schild != NULL) {
		/* check for collision in its new table */
		vsocket_tp collision = g_hash_table_lookup(ht, &(schild->key));
		if(collision != NULL){
			error("hash collision!");
			return;
		}

        gpointer key = &(schild->key);
		g_hash_table_insert(ht, key, schild);
	}
}

