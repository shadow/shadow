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
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>

#include "shadow.h"

static vsocket_tp vsocket_mgr_find_socket_helper(vinterface_tp vi, guint8 protocol,
		in_addr_t remote_addr, in_port_t remote_port, in_port_t local_port);

vsocket_mgr_tp vsocket_mgr_create(in_addr_t addr, guint32 KBps_down, guint32 KBps_up,
		guint64 cpu_speed_Bps) {
	vsocket_mgr_tp net = malloc(sizeof(vsocket_mgr_t));

	net->addr = addr;
	inet_ntop(AF_INET, &addr, net->addr_string, sizeof(net->addr_string));

	net->next_sock_desc = VNETWORK_MIN_SD;
	net->next_rnd_port = VSOCKET_MIN_RND_PORT;
	net->vsockets = g_hash_table_new(g_int16_hash, g_int16_equal);
	net->destroyed_descs = g_hash_table_new(g_int_hash, g_int_equal);

	net->ethernet = vsocket_mgr_create_interface(net, net->addr);
	net->loopback = vsocket_mgr_create_interface(net,htonl(INADDR_LOOPBACK));

	net->vt_mgr = vtransport_mgr_create(net, KBps_down, KBps_up);
	net->vp_mgr = vpacket_mgr_create();
	net->vpipe_mgr = vpipe_mgr_create(net->addr);
	net->vev_mgr = vevent_mgr_create();
	net->vcpu = vcpu_create(cpu_speed_Bps);

	return net;
}

void vsocket_mgr_destroy(vsocket_mgr_tp net) {
	if(net != NULL){
		/* TODO
		 * dsetruction of interfaces should be refactored
		 *
		 * FIXME
		 * this leaks memory - we cant walk both the eth and loop tcp_servers
		 * because they both destroy the same socket.
		 *
		 * XXX
		 * would be smarter to register a destroy function using g_hash_table_new_full
		 * so that when g_hash_table_destroy is called below, things are automatic
		 */

		/* must destroy tcpserver (and its vsockets) first to avoid double free */
  		g_hash_table_foreach(net->ethernet->tcp_servers, vtcp_server_destroy_cb, NULL);
		g_hash_table_destroy(net->ethernet->tcp_servers);

  		g_hash_table_foreach(net->loopback->tcp_servers, vtcp_server_destroy_cb, NULL);
		g_hash_table_destroy(net->loopback->tcp_servers);

		/* destroys remaining vsockets */
		//g_hash_table_foreach(net->vsockets, (GHFunc)vsocket_mgr_destroy_socket_cb, NULL);
		g_hash_table_destroy(net->vsockets);

		/* all vsockets were destroyed, but make sure to free keys */
		g_hash_table_remove_all(net->ethernet->tcp_vsockets);
		g_hash_table_destroy(net->ethernet->tcp_vsockets);
		g_hash_table_remove_all(net->ethernet->udp_vsockets);
		g_hash_table_destroy(net->ethernet->udp_vsockets);
		g_hash_table_remove_all(net->loopback->tcp_vsockets);
		g_hash_table_destroy(net->loopback->tcp_vsockets);
		g_hash_table_remove_all(net->loopback->udp_vsockets);
		g_hash_table_destroy(net->loopback->udp_vsockets);

		/* do not walk since no values were created and stored here */
		g_hash_table_destroy(net->destroyed_descs);

		vpipe_mgr_destroy(net->vpipe_mgr);
		vtransport_mgr_destroy(net->vt_mgr);
		vpacket_mgr_destroy(net->vp_mgr);
		vevent_mgr_destroy(net->vev_mgr);
		vcpu_destroy(net->vcpu);

		net->addr = 0;
		memset(&net->addr_string, 0, sizeof(net->addr_string));
		net->next_sock_desc = 0;
		net->next_rnd_port = 0;

		free(net);
		net = NULL;
	}
}

vinterface_tp vsocket_mgr_create_interface(vsocket_mgr_tp net, in_addr_t addr) {
	if(net != NULL) {
		vinterface_tp vi = malloc(sizeof(vinterface_t));
		vi->tcp_vsockets = g_hash_table_new_full(g_int16_hash, g_int16_equal, g_free, NULL);
		vi->udp_vsockets = g_hash_table_new_full(g_int16_hash, g_int16_equal, g_free, NULL);
		vi->tcp_servers = g_hash_table_new(g_int16_hash, g_int16_equal);
		vi->ip_address = addr;
		return vi;
	}
	return NULL;
}

vsocket_tp vsocket_mgr_create_socket(vsocket_mgr_tp net, guint8 type) {
	vsocket_tp sock = malloc(sizeof(vsocket_t));

	sock->type = type;

	sock->sock_desc = net->next_sock_desc++;
	sock->sock_desc_parent = 0;
	sock->ethernet_peer = NULL;
	sock->loopback_peer = NULL;
	sock->do_delete = 0;
	sock->is_active = 1;

	/* vtransport needs vepoll to be created already */
	sock->vep = vepoll_create(net->vev_mgr, net->addr, sock->sock_desc);
	sock->vt = vtransport_create(net, sock);

	if(type == SOCK_STREAM) {
		sock->curr_state = VTCP_CLOSED;
		vsocket_transition(sock, VTCP_CLOSED);
	} else {
		sock->curr_state = VUDP;
		vsocket_transition(sock, VUDP);
	}
	debug("created socket %u", sock->sock_desc);

	return sock;
}

void vsocket_mgr_destroy_socket(vsocket_tp sock) {
	if(sock != NULL) {
		vpeer_destroy(sock->ethernet_peer);
		vpeer_destroy(sock->loopback_peer);
		vtransport_destroy(sock->vt);

		vepoll_destroy(sock->vep);

		debug("destroyed socket %u", sock->sock_desc);

		memset(sock, 0, sizeof(vsocket_t));
		free(sock);
	}
}

void vsocket_mgr_add_server(vsocket_mgr_tp net, vtcp_server_tp server) {
	if(net != NULL && server != NULL) {
		if(server->sock->ethernet_peer != NULL) {
			g_hash_table_insert(net->ethernet->tcp_servers, &(server->sock->ethernet_peer->port), server);
		}
		if(server->sock->loopback_peer != NULL) {
			g_hash_table_insert(net->loopback->tcp_servers, &(server->sock->loopback_peer->port), server);
		}
	}
}

vtcp_server_tp vsocket_mgr_get_server(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net != NULL && sock != NULL) {
		if(sock->ethernet_peer != NULL) {
			return g_hash_table_lookup(net->ethernet->tcp_servers, &(sock->ethernet_peer->port));
		} else if(sock->loopback_peer != NULL) {
			return g_hash_table_lookup(net->loopback->tcp_servers, &(sock->loopback_peer->port));
		}
	}
	return NULL;
}

void vsocket_mgr_remove_server(vsocket_mgr_tp net, vtcp_server_tp server) {
	if(net != NULL && server != NULL) {
		if(server->sock->ethernet_peer != NULL) {
			g_hash_table_remove(net->ethernet->tcp_servers, &(server->sock->ethernet_peer->port));
		}
		if(server->sock->loopback_peer != NULL) {
			g_hash_table_remove(net->loopback->tcp_servers, &(server->sock->loopback_peer->port));
		}
	}
}

void vsocket_mgr_add_socket(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net != NULL && sock != NULL) {
		g_hash_table_insert(net->vsockets, &(sock->sock_desc), sock);
	}
}

vsocket_tp vsocket_mgr_get_socket(vsocket_mgr_tp net, guint16 sockd) {
        vsocket_tp ret = NULL;
	if(net != NULL) {
		ret = g_hash_table_lookup(net->vsockets, &(sockd));
	}
	return ret;
}

void vsocket_mgr_remove_socket(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net != NULL && sock != NULL) {
		g_hash_table_remove(net->vsockets, &(sock->sock_desc));
	}
}

void vsocket_mgr_destroy_socket_cb(gpointer key, gpointer value, gpointer param) {
	vsocket_mgr_destroy_socket((vsocket_tp) value);
}

void vsocket_mgr_destroy_and_remove_socket(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net == NULL || sock == NULL) {
		return;
	}

	if(g_hash_table_lookup(net->vsockets, &(sock->sock_desc)) == NULL) {
		return;
	}
        g_hash_table_remove(net->vsockets, &sock->sock_desc);

	if(sock->type == SOCK_STREAM) {
		if(sock->ethernet_peer != NULL && net->ethernet != NULL) {
			g_hash_table_remove(net->ethernet->tcp_vsockets, &(sock->ethernet_peer->port));
		}
		if(sock->loopback_peer != NULL && net->loopback != NULL) {
			g_hash_table_remove(net->loopback->tcp_vsockets, &(sock->loopback_peer->port));
		}

		/* child of a server */
		if(sock->sock_desc_parent != 0) {
			vsocket_tp parent = g_hash_table_lookup(net->vsockets, &(sock->sock_desc_parent));

			if(parent != NULL) {
				/* get the server running on the parent */
				vtcp_server_tp parent_server = NULL;

				if(parent->ethernet_peer != NULL && net->ethernet != NULL) {
					parent_server = g_hash_table_lookup(net->ethernet->tcp_servers, &(parent->ethernet_peer->port));
				} else if(parent->loopback_peer != NULL && net->loopback != NULL) {
					parent_server = g_hash_table_lookup(net->loopback->tcp_servers, &(parent->loopback_peer->port));
				}

				if(parent_server != NULL) {
					if(sock->vt != NULL && sock->vt->vtcp != NULL && sock->vt->vtcp->remote_peer != NULL) {
						vtcp_server_child_tp schild = vtcp_server_get_child(parent_server,
								sock->vt->vtcp->remote_peer->addr, sock->vt->vtcp->remote_peer->port);
						vtcp_server_destroy_child(parent_server, schild);
					}
				}

				/* check if deleting this child means the parent should be deleted */
				vsocket_try_destroy_server(net, parent);
			}
		}

		/* a server itself, these two will point to the same server */
		vtcp_server_tp server1 = NULL;
		vtcp_server_tp server2 = NULL;
		if(sock->ethernet_peer != NULL && net->ethernet != NULL) {
			server1 = g_hash_table_lookup(net->ethernet->tcp_servers, &(sock->ethernet_peer->port));
			g_hash_table_remove(net->ethernet->tcp_servers, &sock->ethernet_peer->port);
		}
		if(sock->loopback_peer != NULL && net->loopback != NULL) {
			server2 = g_hash_table_lookup(net->loopback->tcp_servers, &(sock->loopback_peer->port));
			g_hash_table_remove(net->loopback->tcp_servers, &sock->loopback_peer->port);
		}

		/* make sure to only destroy once */
		if(server1 != NULL) {
			vtcp_server_destroy(server1);
		} else if(server2 != NULL) {
			vtcp_server_destroy(server2);
		}
	} else {
		if(sock->ethernet_peer != NULL && net->ethernet != NULL) {
			g_hash_table_remove(net->ethernet->udp_vsockets, &(sock->ethernet_peer->port));
		}
		if(sock->loopback_peer != NULL && net->loopback != NULL) {
			g_hash_table_remove(net->loopback->udp_vsockets, &(sock->loopback_peer->port));
		}
	}

	/* keep track of destroyed sockets for when client calls close */
	if(sock->curr_state != VTCP_CLOSING &&
			sock->prev_state != VTCP_CLOSING) {
		/* use net as dummy value.
		 * TODO: hashtable should really implement a contains() function instead.
		 */
		gint *key = gint_key(sock->sock_desc);
		g_hash_table_insert(net->destroyed_descs, key, net);
	}
	vsocket_mgr_destroy_socket(sock);
}

void vsocket_mgr_destroy_and_remove_socket_cb(gpointer key, gpointer value, gpointer param) {
	vsocket_mgr_destroy_and_remove_socket((vsocket_mgr_tp) param, (vsocket_tp) value);
}

void vsocket_mgr_try_destroy_socket(vsocket_mgr_tp net, vsocket_tp sock) {
	/* we only want to destroy the socket if all its data has been handled */
	if(net != NULL && sock != NULL) {
		if(sock->do_delete) {
			if(vtransport_is_empty(sock->vt)) {
				vsocket_mgr_destroy_and_remove_socket(net, sock);
			}
		}
	}
}

vsocket_tp vsocket_mgr_get_socket_receiver(vsocket_mgr_tp net, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	vpacket_tp packet = vpacket_mgr_lockcontrol(rc_packet, LC_OP_READLOCK | LC_TARGET_PACKET);

	vsocket_tp sock = NULL;
	if(packet != NULL) {
		/* caller is the receiver of the packet */
		sock = vsocket_mgr_find_socket(net, packet->header.protocol,
				packet->header.source_addr, packet->header.source_port,
				packet->header.destination_port);
		vpacket_mgr_lockcontrol(rc_packet, LC_OP_READUNLOCK | LC_TARGET_PACKET);
	}

	rc_vpacket_pod_release_stack(rc_packet);
	return sock;
}

static vsocket_tp vsocket_mgr_find_socket_helper(vinterface_tp vi, guint8 protocol,
		in_addr_t remote_addr, in_port_t remote_port, in_port_t local_port) {
	if(vi != NULL){
		/* get the descriptor for the destination of the packet */
		vsocket_tp target = NULL;
        gconstpointer key = &(local_port);
		if(protocol == SOCK_STREAM) {
			/* check if target is actually a server, or a multiplexed socket */
			vtcp_server_tp server = g_hash_table_lookup(vi->tcp_servers, key);
			vtcp_server_child_tp schild = vtcp_server_get_child(server, remote_addr, remote_port);

			if(schild == NULL) {
				/* target must be the server itself */
				target = g_hash_table_lookup(vi->tcp_vsockets, key);
			} else {
				target = schild->sock;
			}
		} else {
			target = g_hash_table_lookup(vi->udp_vsockets, key);
		}

		return target;
	}
	return NULL;
}

vsocket_tp vsocket_mgr_find_socket(vsocket_mgr_tp net, guint8 protocol,
		in_addr_t remote_addr, in_port_t remote_port, in_port_t local_port) {
	if(net != NULL){
		if(net->loopback != NULL && remote_addr == net->loopback->ip_address) {
			return vsocket_mgr_find_socket_helper(net->loopback, protocol, remote_addr, remote_port, local_port);
		} else {
			return vsocket_mgr_find_socket_helper(net->ethernet, protocol, remote_addr, remote_port, local_port);
		}
	}
	return NULL;
}

guint8 vsocket_mgr_isbound_loopback(vsocket_mgr_tp net, in_port_t port) {
	if(net != NULL && net->loopback != NULL &&
			g_hash_table_lookup(net->loopback->tcp_vsockets, &(port)) != NULL) {
		return 1;
	}
	return 0;
}

guint8 vsocket_mgr_isbound_ethernet(vsocket_mgr_tp net, in_port_t port) {
	if(net != NULL && net->ethernet != NULL &&
			g_hash_table_lookup(net->ethernet->tcp_vsockets, &(port)) != NULL) {
		return 1;
	}
	return 0;
}

void vsocket_mgr_bind_ethernet(vsocket_mgr_tp net, vsocket_tp sock, in_port_t bind_port) {
	if(net != NULL && sock != NULL && net->ethernet != NULL) {
		sock->ethernet_peer = vpeer_create(net->ethernet->ip_address, bind_port);
		gpointer key = gint_key(bind_port);
		if(sock->type == SOCK_STREAM) {
			g_hash_table_insert(net->ethernet->tcp_vsockets, key, sock);
		} else {
			g_hash_table_insert(net->ethernet->udp_vsockets, key, sock);
		}
	}
}

void vsocket_mgr_bind_loopback(vsocket_mgr_tp net, vsocket_tp sock, in_port_t bind_port) {
	if(net != NULL && sock != NULL && net->ethernet != NULL) {
		sock->loopback_peer = vpeer_create(net->loopback->ip_address, bind_port);
		gpointer key = gint_key(bind_port);
		if(sock->type == SOCK_STREAM) {
			g_hash_table_insert(net->loopback->tcp_vsockets, key, sock);
		} else {
			g_hash_table_insert(net->loopback->udp_vsockets, key, sock);
		}
	}
}
void vsocket_mgr_print_stat(vsocket_mgr_tp net, guint16 sockd) {
	if(net != NULL) {
		debug("######looking for stats for socket %u######", sockd);
		vsocket_tp sock = vsocket_mgr_get_socket(net, sockd);
		if(sock != NULL) {
			if(sock->loopback_peer != NULL) {
				debug("sockd %u running on %s:%u", sockd,
					NTOA(sock->loopback_peer->addr), ntohs(sock->loopback_peer->port));
			}

			if(sock->ethernet_peer != NULL) {
				debug("sockd %u running on %s:%u", sockd,
						NTOA(sock->ethernet_peer->addr), ntohs(sock->ethernet_peer->port));
			}

			if(sock->sock_desc_parent > 0) {
				debug("sockd %u has parent sockd %u", sockd, sock->sock_desc_parent);
				vsocket_tp parent = vsocket_mgr_get_socket(net, sock->sock_desc_parent);

				if(parent != NULL) {
					if(parent->loopback_peer != NULL) {
						debug("parent sockd %u running on %s:%u", parent->sock_desc,
								NTOA(parent->loopback_peer->addr), ntohs(parent->loopback_peer->port));
					}

					if(parent->ethernet_peer != NULL) {
						debug("parent sockd %u running on %s:%u", parent->sock_desc,
								NTOA(parent->ethernet_peer->addr), ntohs(parent->ethernet_peer->port));
					}

				} else {
					debug("parent sockd NOT FOUND!");
				}
			}

			if(sock->vt != NULL && sock->vt->vtcp != NULL && sock->vt->vtcp->remote_peer != NULL) {
				debug("sockd %u connected to %s:%u", sockd,
						NTOA(sock->vt->vtcp->remote_peer->addr), ntohs(sock->vt->vtcp->remote_peer->port));
			}

			vtcp_server_tp server = vsocket_mgr_get_server(net, sock);
			if(server != NULL) {
				debug("sockd %u running a server with %u accepted, %u pending, %u incomplete", sockd,
					g_hash_table_size(server->accepted_children), g_queue_get_length(server->pending_queue), 
					g_hash_table_size(server->incomplete_children));

				if(g_queue_get_length(server->pending_queue) > 0 && !(sock->vep->available & VEPOLL_READ)) {
					error("sockd %u should be marked available!!!", sockd);
				}
			}

			if(vepoll_query_available(sock->vep, VEPOLL_READ)) {
				debug("sockd %u ready to read", sockd);
				if(sock->vep->state == VEPOLL_ACTIVE) {
					if(sock->vep->flags & VEPOLL_NOTIFY_SCHEDULED) {
						debug("sockd %u readable, active and notify is scheduled", sockd);
					} else {
						warning("sockd %u read available and active but not scheduled!!!!!", sockd);
					}
				} else {
					debug("sockd %u inactive", sockd);
				}
			}
			if(vepoll_query_available(sock->vep, VEPOLL_WRITE)) {
				debug("sockd %u ready to write", sockd);
				if(sock->vep->state == VEPOLL_ACTIVE) {
					if(sock->vep->flags & VEPOLL_NOTIFY_SCHEDULED) {
						debug("sockd %u writable, active and notify is scheduled", sockd);
					} else {
						warning("sockd %u write available and active but not scheduled!!!!!", sockd);
					}
				} else {
					debug("sockd %u inactive", sockd);
				}
			}
		}

		debug("######stat done for socket %u######", sockd);
	}
}
