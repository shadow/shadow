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

#include <stdlib.h>
#include <stdint.h>
#include <socket.h>
#include <netinet/in.h>
#include <string.h>

#include "vtcp_server.h"
#include "vsocket_mgr.h"
#include "vsocket_mgr_server.h"
#include "vpeer.h"
#include "vtransport.h"
#include "vtcp.h"
#include "hashtable.h"
#include "list.h"
#include "log.h"
#include "sysconfig.h"

static void vtcp_server_add_child_helper(hashtable_tp ht, vtcp_server_child_tp schild);

vtcp_server_tp vtcp_server_create(vsocket_mgr_tp vsocket_mgr, vsocket_tp sock, int backlog) {
	vtcp_server_tp server = malloc(sizeof(vtcp_server_t));

	/* silently truncate backlog at our max level of SOMAXCONN */
//	turn this off for now - we are unable to start many nodes at once otherwise
//	if(backlog > 0 && backlog < SOMAXCONN){
//		server->backlog = (uint8_t) backlog;
//	} else {
//		server->backlog = SOMAXCONN;
//	}

	server->vsocket_mgr = vsocket_mgr;
	server->sock = sock;

	server->incomplete_children = hashtable_create(sysconfig_get_int("vtcpserver_incomplete_hashsize"), sysconfig_get_float("vtcpserver_incomplete_hashgrowth"));
	server->pending_children = hashtable_create(sysconfig_get_int("vtcpserver_pending_hashsize"), sysconfig_get_float("vtcpserver_pending_hashgrowth"));
	server->pending_queue = list_create();
	server->accepted_children = hashtable_create(sysconfig_get_int("vtcpserver_accepted_hashsize"), sysconfig_get_float("vtcpserver_accepted_hashgrowth"));

	return server;
}

void vtcp_server_destroy_cb(void* value, int key) {
	vtcp_server_destroy((vtcp_server_tp) value);
}

void vtcp_server_destroy(vtcp_server_tp server) {
	if(server != NULL){
//		server->backlog = 0;

		hashtable_walk_param(server->incomplete_children, &vsocket_mgr_destroy_and_remove_socket_cb, server->vsocket_mgr);
		hashtable_walk_param(server->pending_children, &vsocket_mgr_destroy_and_remove_socket_cb, server->vsocket_mgr);
		hashtable_walk_param(server->accepted_children, &vsocket_mgr_destroy_and_remove_socket_cb, server->vsocket_mgr);

		hashtable_destroy(server->incomplete_children);
		hashtable_destroy(server->pending_children);
		hashtable_destroy(server->accepted_children);

		/* vsockets stored in pending queue were just deleted from hashtable */
		list_destroy(server->pending_queue);

		free(server);
	}
}

uint8_t vtcp_server_is_empty(vtcp_server_tp server) {
	if(server != NULL &&
			server->accepted_children->population +
			server->incomplete_children->population +
			server->pending_children->population > 0) {
		return 0;
	} else {
		return 1;
	}
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

	int result = vsocket_bind(server->vsocket_mgr, schild->sock->sock_desc, &newaddr, sizeof(newaddr));

	/* if there was an error in bind, cleanup mapping added in socket() */
	if(result == VSOCKET_ERROR){
		dlogf(LOG_WARN, "vsocket_execute_receive: unable to create new connection as requested\n");
		vsocket_mgr_destroy_and_remove_socket(server->vsocket_mgr, schild->sock);
		return NULL;
	}

	/* attach it to connection, dont call connect as that will start new handshake */
	schild->sock->sock_desc_parent = server->sock->sock_desc;

	debugf("vtcp_server_create_child: creating multiplexed socket sd %u for server sd %u\n",
			schild->sock->sock_desc, schild->sock->sock_desc_parent);

	return schild;
}

void vtcp_server_destroy_child(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL && schild != NULL && schild->sock != NULL){
		debugf("vtcp_server_destroy_child: destroying multiplexed socket sd %u for server sd %u\n",
					schild->sock->sock_desc, schild->sock->sock_desc_parent);

		/* remove all possible links to child */
		hashtable_remove(server->incomplete_children, schild->key);
		hashtable_remove(server->pending_children, schild->key);
		hashtable_remove(server->accepted_children, schild->key);

		/* TODO do something smarter instead of re-creating the
		 * pending queue... like a list iterator */
		if(list_get_size(server->pending_queue) > 0) {
			list_tp new_pending = list_create();
			while(list_get_size(server->pending_queue) > 0) {
				vsocket_tp next = list_pop_front(server->pending_queue);
				if(next->sock_desc != schild->sock->sock_desc) {
					list_push_back(new_pending, next);
				}
			}
			list_destroy(server->pending_queue);
			server->pending_queue = new_pending;
		}

		memset(schild, 0, sizeof(vtcp_server_child_t));
		free(schild);
	}
}

vtcp_server_child_tp vtcp_server_get_child(vtcp_server_tp server, in_addr_t remote_addr, in_port_t remote_port) {
	if(server != NULL) {
		unsigned int hashkey = vsocket_hash(remote_addr, remote_port);

		vtcp_server_child_tp target = NULL;

		/* look through existing connections */
		target = hashtable_get(server->accepted_children, hashkey);
		if(target == NULL){
			target = hashtable_get(server->incomplete_children, hashkey);
			if(target == NULL){
				target = hashtable_get(server->pending_children, hashkey);
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
		hashtable_remove(server->incomplete_children, schild->key);
	}
}

uint8_t vtcp_server_add_child_pending(vtcp_server_tp server, vtcp_server_child_tp schild) {
	if(server != NULL) {
//		Disabled backlog for now
//		if(list_get_size(server->pending_queue) < server->backlog) {
			vtcp_server_add_child_helper(server->pending_children, schild);
			list_push_back(server->pending_queue, schild);
			return 1;
//		}
	}
	return 0;
}

vtcp_server_child_tp vtcp_server_remove_child_pending(vtcp_server_tp server) {
	if(server != NULL && server->pending_queue != NULL) {
		vtcp_server_child_tp pending = list_pop_front(server->pending_queue);
		if(pending != NULL) {
			hashtable_remove(server->pending_children, pending->key);
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
		hashtable_remove(server->accepted_children, schild->key);
	}
}

static void vtcp_server_add_child_helper(hashtable_tp ht, vtcp_server_child_tp schild) {
	if(schild != NULL) {
		/* check for collision in its new table */
		vsocket_tp collision = hashtable_get(ht, schild->key);
		if(collision != NULL){
			dlogf(LOG_ERR, "vtcp_server_add_child_helper: hash collision!\n");
			return;
		}

		hashtable_set(ht, schild->key, schild);
	}
}

