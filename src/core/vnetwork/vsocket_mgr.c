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

#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

#include "log.h"
#include "vsocket_mgr.h"
#include "vsocket.h"
#include "vtransport_mgr.h"
#include "vtransport.h"
#include "vtcp_server.h"
#include "vci.h"
#include "sysconfig.h"
#include "vpipe.h"
#include "vepoll.h"
#include "vevent_mgr.h"
#include "vcpu.h"

static vsocket_tp vsocket_mgr_find_socket_helper(vinterface_tp vi, uint8_t protocol,
		in_addr_t remote_addr, in_port_t remote_port, in_port_t local_port);

vsocket_mgr_tp vsocket_mgr_create(context_provider_tp p, in_addr_t addr, uint32_t KBps_down, uint32_t KBps_up,
		uint64_t cpu_speed_Bps) {
	vsocket_mgr_tp net = malloc(sizeof(vsocket_mgr_t));

	net->addr = addr;
	inet_ntop(AF_INET, &addr, net->addr_string, sizeof(net->addr_string));

	net->next_sock_desc = VNETWORK_MIN_SD;
	net->next_rnd_port = VSOCKET_MIN_RND_PORT;
	net->vsockets = hashtable_create(sysconfig_get_int("vsockets_hashsize"), sysconfig_get_float("vsockets_hashgrowth"));
	net->destroyed_descs = hashtable_create(sysconfig_get_int("vsocket_destroyed_descriptors_hashsize"), sysconfig_get_float("vsocket_destroyed_descriptors_hashgrowth"));

	net->ethernet = vsocket_mgr_create_interface(net, net->addr);
	net->loopback = vsocket_mgr_create_interface(net,htonl(INADDR_LOOPBACK));

	net->vt_mgr = vtransport_mgr_create(net, KBps_down, KBps_up);
	net->vp_mgr = vpacket_mgr_create();
	net->vpipe_mgr = vpipe_mgr_create(net->addr);
	net->vev_mgr = vevent_mgr_create(p);
	net->vcpu = vcpu_create(cpu_speed_Bps);

	return net;
}

void vsocket_mgr_destroy(vsocket_mgr_tp net) {
	if(net != NULL){
		/* TODO dsetruction of interfaces should be refactored */

		/* FIXME this leaks memory - we cant walk both the eth and loop tcp_servers
		 * because they both destroy the same socket.
		 */

		/* must destroy tcpserver (and its vsockets) first to avoid double free */
//		hashtable_walk(net->ethernet->tcp_servers, &vtcp_server_destroy_cb);
		hashtable_destroy(net->ethernet->tcp_servers);

//		hashtable_walk(net->loopback->tcp_servers, &vtcp_server_destroy_cb);
		hashtable_destroy(net->loopback->tcp_servers);

		/* destroys remaining vsockets */
		hashtable_walk(net->vsockets, &vsocket_mgr_destroy_socket_cb);
		hashtable_destroy(net->vsockets);

		/* since all vsockets were destroyed, we can simply remove references here */
		hashtable_destroy(net->ethernet->tcp_vsockets);
		hashtable_destroy(net->ethernet->udp_vsockets);
		hashtable_destroy(net->loopback->tcp_vsockets);
		hashtable_destroy(net->loopback->udp_vsockets);

		/* do not walk since no values were created and stored here */
		hashtable_destroy(net->destroyed_descs);

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

in_port_t vsocket_mgr_get_random_port(vsocket_mgr_tp net) {
	assert(net);
	in_port_t p = net->next_rnd_port++;
	assert(p >= VSOCKET_MIN_RND_PORT);
	return p;
}

int vsocket_mgr_get_random_descriptor(vsocket_mgr_tp net) {
	assert(net);
	/*
	 * if this looped over because of long simulations, scream!
	 * @todo implement some kind of descriptor tracking to reuse old ones.
	 */
	int d = net->next_sock_desc++;
	assert(d >= VNETWORK_MIN_SD);
	return d;
}

vinterface_tp vsocket_mgr_create_interface(vsocket_mgr_tp net, in_addr_t addr) {
	if(net != NULL) {
		vinterface_tp vi = malloc(sizeof(vinterface_t));
		vi->tcp_vsockets = hashtable_create(sysconfig_get_int("vsocket_tcp_hashsize"), sysconfig_get_float("vsocket_tcp_hashgrowth"));
		vi->udp_vsockets = hashtable_create(sysconfig_get_int("vsocket_udp_hashsize"), sysconfig_get_float("vsocket_udp_hashgrowth"));
		vi->tcp_servers = hashtable_create(sysconfig_get_int("vsocket_tcpserver_hashsize"), sysconfig_get_float("vsocket_tcpserver_hashgrowth"));
		vi->ip_address = addr;
		return vi;
	}
	return NULL;
}

vsocket_tp vsocket_mgr_create_socket(vsocket_mgr_tp net, uint8_t type) {
	vsocket_tp sock = malloc(sizeof(vsocket_t));

	sock->type = type;

	sock->sock_desc = vsocket_mgr_get_random_descriptor(net);
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
	debugf("vsocket_mgr_create_socket: created socket %u\n", sock->sock_desc);

	return sock;
}

void vsocket_mgr_destroy_socket(vsocket_tp sock) {
	if(sock != NULL) {

		vpeer_destroy(sock->ethernet_peer);
		vpeer_destroy(sock->loopback_peer);
		vtransport_destroy(sock->vt);

		vepoll_destroy(sock->vep);

		debugf("vsocket_mgr_destroy_socket: destroyed socket %u\n", sock->sock_desc);

		memset(sock, 0, sizeof(vsocket_t));
		free(sock);
	}
}

void vsocket_mgr_add_server(vsocket_mgr_tp net, vtcp_server_tp server) {
	if(net != NULL && server != NULL) {
		if(server->sock->ethernet_peer != NULL) {
			hashtable_set(net->ethernet->tcp_servers, server->sock->ethernet_peer->port, server);
		}
		if(server->sock->loopback_peer != NULL) {
			hashtable_set(net->loopback->tcp_servers, server->sock->loopback_peer->port, server);
		}
	}
}

vtcp_server_tp vsocket_mgr_get_server(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net != NULL && sock != NULL) {
		if(sock->ethernet_peer != NULL) {
			return hashtable_get(net->ethernet->tcp_servers, sock->ethernet_peer->port);
		} else if(sock->loopback_peer != NULL) {
			return hashtable_get(net->loopback->tcp_servers, sock->loopback_peer->port);
		}
	}
	return NULL;
}

void vsocket_mgr_remove_server(vsocket_mgr_tp net, vtcp_server_tp server) {
	if(net != NULL && server != NULL) {
		if(server->sock->ethernet_peer != NULL) {
			hashtable_remove(net->ethernet->tcp_servers, server->sock->ethernet_peer->port);
		}
		if(server->sock->loopback_peer != NULL) {
			hashtable_remove(net->loopback->tcp_servers, server->sock->loopback_peer->port);
		}
	}
}

void vsocket_mgr_add_socket(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net != NULL && sock != NULL) {
		hashtable_set(net->vsockets, (unsigned int)sock->sock_desc, sock);
	}
}

vsocket_tp vsocket_mgr_get_socket(vsocket_mgr_tp net, int sockd) {
	if(net != NULL) {
		return hashtable_get(net->vsockets, (unsigned int)sockd);
	}
	return NULL;
}

void vsocket_mgr_remove_socket(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net != NULL && sock != NULL) {
		hashtable_remove(net->vsockets, (unsigned int)sock->sock_desc);
	}
}

void vsocket_mgr_destroy_socket_cb(void* value, int key) {
	vsocket_mgr_destroy_socket((vsocket_tp) value);
}

void vsocket_mgr_destroy_and_remove_socket(vsocket_mgr_tp net, vsocket_tp sock) {
	if(net == NULL || sock == NULL) {
		return;
	}

	if(hashtable_remove(net->vsockets, (unsigned int)sock->sock_desc) == NULL) {
		return;
	}

	if(sock->type == SOCK_STREAM) {
		if(sock->ethernet_peer != NULL && net->ethernet != NULL) {
			hashtable_remove(net->ethernet->tcp_vsockets, (unsigned int)sock->ethernet_peer->port);
		}
		if(sock->loopback_peer != NULL && net->loopback != NULL) {
			hashtable_remove(net->loopback->tcp_vsockets, (unsigned int)sock->loopback_peer->port);
		}

		/* child of a server */
		if(sock->sock_desc_parent != 0) {
			vsocket_tp parent = hashtable_get(net->vsockets, (unsigned int)sock->sock_desc_parent);

			if(parent != NULL) {
				/* get the server running on the parent */
				vtcp_server_tp parent_server = NULL;

				if(parent->ethernet_peer != NULL && net->ethernet != NULL) {
					parent_server = hashtable_get(net->ethernet->tcp_servers, (unsigned int)parent->ethernet_peer->port);
				} else if(parent->loopback_peer != NULL && net->loopback != NULL) {
					parent_server = hashtable_get(net->loopback->tcp_servers,(unsigned int) parent->loopback_peer->port);
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
			server1 = hashtable_remove(net->ethernet->tcp_servers, (unsigned int)sock->ethernet_peer->port);
		}
		if(sock->loopback_peer != NULL && net->loopback != NULL) {
			server2 = hashtable_remove(net->loopback->tcp_servers, (unsigned int)sock->loopback_peer->port);
		}

		/* make sure to only destroy once */
		if(server1 != NULL) {
			vtcp_server_destroy(server1);
		} else if(server2 != NULL) {
			vtcp_server_destroy(server2);
		}
	} else {
		if(sock->ethernet_peer != NULL && net->ethernet != NULL) {
			hashtable_remove(net->ethernet->udp_vsockets, (unsigned int)sock->ethernet_peer->port);
		}
		if(sock->loopback_peer != NULL && net->loopback != NULL) {
			hashtable_remove(net->loopback->udp_vsockets, (unsigned int)sock->loopback_peer->port);
		}
	}

	/* keep track of destroyed sockets for when client calls close */
	if(sock->curr_state != VTCP_CLOSING &&
			sock->prev_state != VTCP_CLOSING) {
		/* use net as dummy value.
		 * TODO: hashtable should really implement a contains() function instead.
		 */
		hashtable_set(net->destroyed_descs, (unsigned int)sock->sock_desc, net);
	}
	vsocket_mgr_destroy_socket(sock);
}

void vsocket_mgr_destroy_and_remove_socket_cb(void* value, int key, void* param) {
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

static vsocket_tp vsocket_mgr_find_socket_helper(vinterface_tp vi, uint8_t protocol,
		in_addr_t remote_addr, in_port_t remote_port, in_port_t local_port) {
	if(vi != NULL){
		/* get the descriptor for the destination of the packet */
		vsocket_tp target = NULL;
		if(protocol == SOCK_STREAM) {
			/* check if target is actually a server, or a multiplexed socket */
			vtcp_server_tp server = hashtable_get(vi->tcp_servers, local_port);
			vtcp_server_child_tp schild = vtcp_server_get_child(server, remote_addr, remote_port);

			if(schild == NULL) {
				/* target must be the server itself */
				target = hashtable_get(vi->tcp_vsockets, (unsigned int)local_port);
			} else {
				target = schild->sock;
			}
		} else {
			target = hashtable_get(vi->udp_vsockets, (unsigned int)local_port);
		}

		return target;
	}
	return NULL;
}

vsocket_tp vsocket_mgr_find_socket(vsocket_mgr_tp net, uint8_t protocol,
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

uint8_t vsocket_mgr_isbound_loopback(vsocket_mgr_tp net, in_port_t port) {
	if(net != NULL && net->loopback != NULL &&
			hashtable_get(net->loopback->tcp_vsockets, port) != NULL) {
		return 1;
	}
	return 0;
}

uint8_t vsocket_mgr_isbound_ethernet(vsocket_mgr_tp net, in_port_t port) {
	if(net != NULL && net->ethernet != NULL &&
			hashtable_get(net->ethernet->tcp_vsockets, port) != NULL) {
		return 1;
	}
	return 0;
}

void vsocket_mgr_bind_ethernet(vsocket_mgr_tp net, vsocket_tp sock, in_port_t bind_port) {
	if(net != NULL && sock != NULL && net->ethernet != NULL) {
		sock->ethernet_peer = vpeer_create(net->ethernet->ip_address, bind_port);
		if(sock->type == SOCK_STREAM) {
			hashtable_set(net->ethernet->tcp_vsockets, bind_port, sock);
		} else {
			hashtable_set(net->ethernet->udp_vsockets, bind_port, sock);
		}
	}
}

void vsocket_mgr_bind_loopback(vsocket_mgr_tp net, vsocket_tp sock, in_port_t bind_port) {
	if(net != NULL && sock != NULL && net->ethernet != NULL) {
		sock->loopback_peer = vpeer_create(net->loopback->ip_address, bind_port);
		if(sock->type == SOCK_STREAM) {
			hashtable_set(net->loopback->tcp_vsockets, bind_port, sock);
		} else {
			hashtable_set(net->loopback->udp_vsockets, bind_port, sock);
		}
	}
}

void vsocket_mgr_onnotify(vsocket_mgr_tp net, context_provider_tp provider, int sockd) {
	if(net != NULL) {
		/* check for a pipe */
		vepoll_tp pipe_poll = vpipe_get_poll(net->vpipe_mgr, sockd);
		if(pipe_poll != NULL) {
			vepoll_execute_notification(provider, pipe_poll);
		} else {
			/* o/w a socket */
			vsocket_tp sock = vsocket_mgr_get_socket(net, sockd);
			if(sock != NULL && sock->vep != NULL) {
				vepoll_execute_notification(provider, sock->vep);
			} else {
				dlogf(LOG_INFO, "vepoll_on_notify: socket %u no longer exists, skipping notification.\n", sockd);
			}
		}
	}
}

void vsocket_mgr_print_stat(vsocket_mgr_tp net, int sockd) {
	if(net != NULL) {
		debugf("######vsocket_mgr_print_stat: looking for stats for socket %u######\n", sockd);
		vsocket_tp sock = vsocket_mgr_get_socket(net, sockd);
		if(sock != NULL) {
			if(sock->loopback_peer != NULL) {
				debugf("sockd %i running on %s:%u\n", sockd,
					inet_ntoa_t(sock->loopback_peer->addr), ntohs(sock->loopback_peer->port));
			}

			if(sock->ethernet_peer != NULL) {
				debugf("sockd %i running on %s:%u\n", sockd,
					inet_ntoa_t(sock->ethernet_peer->addr), ntohs(sock->ethernet_peer->port));
			}

			if(sock->sock_desc_parent > 0) {
				debugf("sockd %ii has parent sockd %i\n", sockd, sock->sock_desc_parent);
				vsocket_tp parent = vsocket_mgr_get_socket(net, sock->sock_desc_parent);

				if(parent != NULL) {
					if(parent->loopback_peer != NULL) {
						debugf("parent sockd %i running on %s:%u\n", parent->sock_desc,
							inet_ntoa_t(parent->loopback_peer->addr), ntohs(parent->loopback_peer->port));
					}

					if(parent->ethernet_peer != NULL) {
						debugf("parent sockd %i running on %s:%u\n", parent->sock_desc,
							inet_ntoa_t(parent->ethernet_peer->addr), ntohs(parent->ethernet_peer->port));
					}

				} else {
					debugf("parent sockd NOT FOUND!\n");
				}
			}

			if(sock->vt != NULL && sock->vt->vtcp != NULL && sock->vt->vtcp->remote_peer != NULL) {
				debugf("sockd %i connected to %s:%u\n", sockd,
					inet_ntoa_t(sock->vt->vtcp->remote_peer->addr), ntohs(sock->vt->vtcp->remote_peer->port));
			}

			vtcp_server_tp server = vsocket_mgr_get_server(net, sock);
			if(server != NULL) {
				debugf("sockd %i running a server with %u accepted, %u pending, %u incomplete\n", sockd,
					server->accepted_children->population, server->pending_queue->num_elems,
					server->incomplete_children->population);

				if(server->pending_queue->num_elems > 0 && !(sock->vep->available & VEPOLL_READ)) {
					dlogf(LOG_ERR, "sockd %u should be marked available!!!\n", sockd);
				}
			}

			if(vepoll_query_available(sock->vep, VEPOLL_READ)) {
				debugf("sockd %i ready to read\n", sockd);
				if(sock->vep->state == VEPOLL_ACTIVE) {
					if(sock->vep->flags & VEPOLL_NOTIFY_SCHEDULED) {
						debugf("sockd %i readable, active and notify is scheduled\n", sockd);
					} else {
						dlogf(LOG_WARN, "sockd %i read available and active but not scheduled!!!!!\n", sockd);
					}
				} else {
					debugf("sockd %i inactive\n", sockd);
				}
			}
			if(vepoll_query_available(sock->vep, VEPOLL_WRITE)) {
				debugf("sockd %i ready to write\n", sockd);
				if(sock->vep->state == VEPOLL_ACTIVE) {
					if(sock->vep->flags & VEPOLL_NOTIFY_SCHEDULED) {
						debugf("sockd %i writable, active and notify is scheduled\n", sockd);
					} else {
						dlogf(LOG_WARN, "sockd %i write available and active but not scheduled!!!!!\n", sockd);
					}
				} else {
					debugf("sockd %i inactive\n", sockd);
				}
			}
		}

		debugf("######vsocket_mgr_print_stat: stat done for socket %i######\n", sockd);
	}
}
