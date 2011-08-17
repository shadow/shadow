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

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>

#include "vsocket_mgr.h"
#include "vsocket_mgr_server.h"
#include "vsocket.h"
#include "log.h"
#include "context.h"
#include "global.h"
#include "vbuffer.h"
#include "vtransport.h"
#include "vtcp.h"
#include "vudp.h"
#include "vtcp_server.h"
#include "vpacket_mgr.h"
#include "vpacket.h"
#include "vci.h"
#include "sim.h"
#include "vepoll.h"
#include "vpipe.h"

static int vsocket_bind_implicit(vsocket_mgr_tp net, int fd, in_addr_t addr);

void vsocket_try_destroy_server(vsocket_mgr_tp net, vsocket_tp server_sock) {
	if(server_sock->type == SOCK_STREAM &&
			server_sock->curr_state == VTCP_CLOSING) {
		vtcp_server_tp server = vsocket_mgr_get_server(net, server_sock);
		if(vtcp_server_is_empty(server)) {
			vsocket_mgr_destroy_and_remove_socket(net, server_sock);
		}
	}
}

unsigned int vsocket_hash(in_addr_t addr, in_port_t port) {
	size_t portsize = sizeof(in_port_t);
	size_t addrsize = sizeof(in_addr_t);

	void* buffer = calloc(1, addrsize + portsize);

	memcpy(buffer, (void*)&addr, addrsize);
	memcpy(buffer+addrsize, (void*)&port, portsize);

	unsigned int hash_value = adler32_hash2(buffer, addrsize + portsize);
	free(buffer);
	return hash_value;
}

#if 0
static vsocket_tp vsocket_get_socket_sender(vsocket_mgr_tp net, rc_vpacket_pod_tp rc_packet) {
	rc_vpacket_pod_retain_stack(rc_packet);
	vpacket_tp packet = rc_vpacket_pod_get(rc_packet);

	vsocket_tp sock = NULL;
	if(packet != NULL) {
		/* caller is the sender of the packet */
		sock = vsocket_mgr_find_socket(net, packet->header.protocol,
				packet->header.destination_addr, packet->header.destination_port,
				packet->header.source_port);
	}

	rc_vpacket_pod_release_stack(rc_packet);
	return sock;
}

void vsocket_notify(vsocket_mgr_tp net, vsocket_tp sock, uint8_t can_read, uint8_t can_write) {
	if(net == NULL || sock == NULL){
		return;
	}

	/* check several cases for not marking the socket available */
	if(sock->curr_state == VTCP_CLOSED || sock->curr_state == VTCP_CLOSING) {
		/* cant read or write if connection is closed or close was called by client */
		debugf("vsocket_notify: not creating notification event, connection CLOSED or CLOSING\n");
		return;
	}
	if(can_write && sock->curr_state == VTCP_CLOSE_WAIT) {
		/* cant write to a connection closed by the other end */
		debugf("vsocket_notify: not creating write notification event, connection CLOSE_WAIT\n");
		return;
	}
	if(can_write && sock->curr_state != VTCP_ESTABLISHED) {
		/* dont tell the user it can write because a control packet was sent
		 * and caused the buffer to become empty
		 */
		debugf("vsocket_notify: not creating write notification event, connection not ESTABLISHED\n");
		return;
	}
	if(!sock->is_active) {
		/* dont mark unaccepted connections as available */
		debugf("vsocket_notify: not creating notification event, socket not active\n");
		return;
	}

	enum vepoll_type activation_type = 0;
	if(can_read) {
		activation_type |= VEPOLL_READ;
	}
	if(can_write) {
		activation_type |= VEPOLL_WRITE;
	}

	if(vepoll_mark_available(sock->vep, activation_type) == 0) {
		debugf("vsocket_notify: event created, socket %i type=%u\n", sock->sock_desc, activation_type);
	}
}
#endif

void vsocket_transition(vsocket_tp sock, enum vsocket_state newstate) {
	if(sock != NULL) {
		sock->prev_state = sock->curr_state;
		sock->curr_state = newstate;

		char* s;
		switch (newstate) {
			case VUDP:
				vepoll_mark_active(sock->vep);
				vepoll_mark_available(sock->vep, VEPOLL_WRITE);
				s = "UDP";
				break;
			case VTCP_CLOSED:
				vepoll_mark_inactive(sock->vep);
				s = "CLOSED";
				break;
			case VTCP_LISTEN:
				vepoll_mark_active(sock->vep);
				s = "LISTEN";
				break;
			case VTCP_SYN_SENT:
				s = "SYN_SENT";
				break;
			case VTCP_SYN_RCVD:
				s = "SYN_RCVD";
				break;
			case VTCP_ESTABLISHED:
				vepoll_mark_active(sock->vep);
				vepoll_mark_available(sock->vep, VEPOLL_WRITE);
				s = "ESTABLISHED";
				break;
			case VTCP_CLOSING:
				vepoll_mark_inactive(sock->vep);
				s = "CLOSING";
				break;
			case VTCP_CLOSE_WAIT:
				/* user needs to read a 0 so it knows we closed */
				vepoll_mark_available(sock->vep, VEPOLL_READ);
				s = "CLOSE_WAIT";
				break;
			default:
				s = "ERROR!";
				break;
		}
		debugf("vsocket_transition: socket %u moved to state %s (parent is %u)\n", sock->sock_desc, s, sock->sock_desc_parent);
	}
}

static int vsocket_bind_implicit(vsocket_mgr_tp net, int fd, in_addr_t addr) {
	struct sockaddr_in bind_addr;
	memset(&bind_addr, 0, sizeof(bind_addr));
	bind_addr.sin_addr.s_addr = addr;
	bind_addr.sin_port = htons(net->next_rnd_port++);
	bind_addr.sin_family = PF_INET;
	return vsocket_bind(net, fd, &bind_addr, sizeof(bind_addr));
}

int vsocket_socket(vsocket_mgr_tp net, int domain, int type, int protocol) {
	/* vsocket only supports PF_INET */
	if (domain != PF_INET) {
		dlogf(LOG_WARN, "vsocket_socket: trying to create socket with domain \"%i\", we only support PF_INET\n", domain);
		errno = EAFNOSUPPORT;
		goto err;
	}

	/* vsocket only supports non-blocking sockets */
	uint8_t blocking = 1;

	/* clear non-blocking flags if set to get true type */
	if(type & SOCK_NONBLOCK) {
		type = type & ~SOCK_NONBLOCK;
		blocking = 0;
	}
	if(type & SOCK_CLOEXEC) {
		type = type & ~SOCK_CLOEXEC;
		blocking = 0;
	}

	/* check for our supported types */
	if (type != SOCK_STREAM && type != SOCK_DGRAM) {
		dlogf(LOG_WARN, "vsocket_socket: trying to create socket with type \"%i\", we only support SOCK_STREAM and SOCK_DGRAM\n", type);
		errno = EPROTONOSUPPORT;
		goto err;
	}

	if(blocking) {
		/* TODO do we require NONBLOCK be set immediately for us to support the socket?
		 * pretty sure Tor uses come ctrl functions at times*/
		dlogf(LOG_WARN, "vsocket_socket: trying to create blocking socket, we only support non-blocking (bitwise OR 'SOCK_NONBLOCK' with type) [%i]\n", type);
		errno = EPROTONOSUPPORT;
		goto err;
	}

	/* create and store the vsocket */
	vsocket_tp sock = vsocket_mgr_create_socket(net, type);
	vsocket_mgr_add_socket(net, sock);

	return sock->sock_desc;

err:
	dlogf(LOG_CRIT, "vsocket_socket: error creating socket, returning an invalid socket descriptor\n");
	return VSOCKET_ERROR;
}

int vsocket_socketpair(vsocket_mgr_tp net, int domain, int type, int protocol, int sv[2]) {
	/* TODO: this is currently implemented just so it works in scallion!! */

	/* create a pair of connected sockets, i.e. a pipe */
	if(domain != AF_UNIX) {
		errno = EAFNOSUPPORT;
		return VSOCKET_ERROR;
	}

	/* vsocket only supports non-blocking sockets */
	uint8_t blocking = 1;

	/* clear non-blocking flags if set to get true type */
	if(type & SOCK_NONBLOCK) {
		type = type & ~SOCK_NONBLOCK;
		blocking = 0;
	}
	if(type & SOCK_CLOEXEC) {
		type = type & ~SOCK_CLOEXEC;
		blocking = 0;
	}

	if(type != SOCK_STREAM) {
		errno = EPROTONOSUPPORT;
		return VSOCKET_ERROR;
	}

	if(blocking) {
		/* TODO do we require NONBLOCK be set immediately for us to support the socket?
		 * pretty sure Tor uses come ctrl functions at times*/
		dlogf(LOG_WARN, "vsocket_socket: trying to create blocking socket, we only support non-blocking (bitwise OR 'SOCK_NONBLOCK' with type) {%i}\n", type);
		errno = EPROTONOSUPPORT;
		return VSOCKET_ERROR;
	}

	/* create the bi-directional pipe */
	vpipe_id fda = net->next_sock_desc++;
	vpipe_id fdb = net->next_sock_desc++;

	if(vpipe_create(net->vev_mgr, net->vpipe_mgr, fda, fdb) == VPIPE_SUCCESS) {
		debugf("vsocket_socketpair: created socketpair (%u, %u)\n", fda, fdb);
		sv[0] = fda;
		sv[1] = fdb;
		return VSOCKET_SUCCESS;
	} else {
		debugf("vsocket_socketpair: vpipe error, socketpair not created\n");
		return VSOCKET_ERROR;
	}
}

int vsocket_bind(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t saddr_len) {
	/* check for NULL addr */
	if(saddr == NULL || saddr_len < sizeof(struct sockaddr_in)) {
		errno = EFAULT;
		return VSOCKET_ERROR;
	}

	in_addr_t bind_addr = saddr->sin_addr.s_addr;
	in_port_t bind_port = saddr->sin_port;

	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	/* check if the socket is already bound */
	if(sock->ethernet_peer != NULL || sock->loopback_peer != NULL) {
		errno = EINVAL;
		return VSOCKET_ERROR;
	}

	if(bind_port == 0) {
		bind_port = htons(net->next_rnd_port++);
	}

	int bound_lb = vsocket_mgr_isbound_loopback(net, bind_port);
	int bound_eth = vsocket_mgr_isbound_ethernet(net, bind_port);

	/* TODO refactor this to separate method.
	 * make sure an existing socket is not already using the port and interface
	 * we can only bind depending on what they ask to bind to */
	if(bind_addr == htonl(INADDR_ANY)) {
		/* must not be an existing socket at the port */
		if(bound_lb || bound_eth) {
			errno = EADDRINUSE;
			return VSOCKET_ERROR;
		} else {
			/* ok to bind to all interfaces */
			vsocket_mgr_bind_loopback(net, sock, bind_port);
			vsocket_mgr_bind_ethernet(net, sock, bind_port);
		}
	} else if(bind_addr == htonl(INADDR_LOOPBACK)) {
		/* if port is taken, loopback must be open */
		if(bound_lb) {
			errno = EADDRINUSE;
			return VSOCKET_ERROR;
		} else {
			/* ok to bind to loopback */
			vsocket_mgr_bind_loopback(net, sock, bind_port);
		}
	} else {
		/* if port is taken, eth must be open */
		if(bound_eth) {
			errno = EADDRINUSE;
			return VSOCKET_ERROR;
		} else {
			/* one last check, better be a valid address */
			if(bind_addr != net->addr) {
				errno = EADDRNOTAVAIL;
				return VSOCKET_ERROR;
			} else {
				/* ok to bind to outgoing interface */
				vsocket_mgr_bind_ethernet(net, sock, bind_port);
			}
		}
	}

	return VSOCKET_SUCCESS;
}

int vsocket_getsockname(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t* saddr_len) {
	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* check for NULL addr */
	if(saddr == NULL) {
		errno = EFAULT;
		return VSOCKET_ERROR;
	}

	if(saddr_len == NULL || *saddr_len < sizeof(struct sockaddr_in)){
		errno = EINVAL;
		return VSOCKET_ERROR;
	}

	/* check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	/* 'return' socket info */
	if(sock->ethernet_peer != NULL && sock->loopback_peer != NULL) {
		saddr->sin_addr.s_addr = htonl(INADDR_ANY);
		saddr->sin_port = sock->ethernet_peer->port;
	} else if(sock->loopback_peer != NULL) {
		saddr->sin_addr.s_addr = sock->loopback_peer->addr;
		saddr->sin_port = sock->loopback_peer->port;
	} else if(sock->ethernet_peer != NULL) {
		saddr->sin_addr.s_addr = sock->ethernet_peer->addr;
		saddr->sin_port = sock->ethernet_peer->port;
	} else {
		errno = EINVAL;
		return VSOCKET_ERROR;
	}
	saddr->sin_family = PF_INET;
	*saddr_len = sizeof(struct sockaddr_in);

	return VSOCKET_SUCCESS;
}

int vsocket_connect(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t saddr_len) {
	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* check for NULL addr */
	if(saddr == NULL || saddr_len < sizeof(struct sockaddr_in)) {
		errno = EFAULT;
		return VSOCKET_ERROR;
	}

	/* check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	if (sock->type == SOCK_STREAM) {
		/* for SOCK_STREAM, saddr is the remote address we want to connect to.
		 * check if the address is correct family */
		if(saddr->sin_family != PF_INET) {
			errno = EAFNOSUPPORT;
			return VSOCKET_ERROR;
		}

		/* check if the port is available
		 * WRONG!! saddr is not the local address!! */
//		if(vsocket_mgr_get_socket_tcp(net, saddr->sin_port) != NULL){
//			errno = EADDRINUSE;
//			return VSOCKET_ERROR;
//		}

		/* check if we are connected or trying to connect */
		if(sock->vt != NULL && sock->vt->vtcp != NULL) {
			/* if we have a remote peer, we have connection status */
			if(sock->vt->vtcp->remote_peer != NULL) {
				if(sock->curr_state == VTCP_ESTABLISHED) {
					errno = EISCONN;
					return VSOCKET_ERROR;
				} else if (sock->vt->vtcp->connection_was_reset) {
					errno = ECONNREFUSED;
					return VSOCKET_ERROR;
				} else {
					errno = EALREADY;
					return VSOCKET_ERROR;
				}
			}
		}

		/* if we don't have a local peer, do an implicit bind to defaults */
		if(sock->ethernet_peer == NULL && sock->loopback_peer == NULL) {
			in_addr_t bind_addr;
			if(saddr->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
				bind_addr = htonl(INADDR_LOOPBACK);
			} else {
				bind_addr = net->addr;
			}
			if(vsocket_bind_implicit(net, fd, bind_addr) != VSOCKET_SUCCESS) {
				return VSOCKET_ERROR;
			}
		}

		/* create the connection state */
		vtcp_connect(sock->vt->vtcp, saddr->sin_addr.s_addr, saddr->sin_port);

		/* send 1st part of 3-way handshake, closed->syn_sent */
		rc_vpacket_pod_tp rc_packet = vtcp_create_packet(sock->vt->vtcp, SYN | CON, 0, NULL);
		uint8_t success = vtcp_send_packet(sock->vt->vtcp, rc_packet);

		rc_vpacket_pod_release(rc_packet);

		if(!success) {
			/* this should never happen, control packets consume no buffer space */
			dlogf(LOG_ERR, "vsocket_connect: error sending SYN step 1\n");
			vtcp_disconnect(sock->vt->vtcp);
			errno = EAGAIN;
			return VSOCKET_ERROR;
		}

		vsocket_transition(sock, VTCP_SYN_SENT);

		/* we dont block, so return EINPROGRESS while waiting for establishment */
		errno = EINPROGRESS;
		return VSOCKET_ERROR;
	} else {
		/* check if the address is correct family */
		if(saddr->sin_family != PF_INET && saddr->sin_family != AF_UNSPEC) {
			errno = EAFNOSUPPORT;
			return VSOCKET_ERROR;
		}

		/* if we don't have a local peer, do an implicit bind to defaults */
		if(sock->ethernet_peer == NULL && sock->loopback_peer == NULL) {
			in_addr_t bind_addr;
			if(saddr->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
				bind_addr = htonl(INADDR_LOOPBACK);
			} else {
				bind_addr = net->addr;
			}
			if(vsocket_bind_implicit(net, fd, bind_addr) != VSOCKET_SUCCESS) {
				return VSOCKET_ERROR;
			}
		}

		/* for UDP, this specifies the default destination for packets */
		if(sock->vt != NULL && sock->vt->vudp != NULL) {
			/* dissolve our existing default destination */
			if(sock->vt->vudp->default_remote_peer != NULL) {
				vpeer_destroy(sock->vt->vudp->default_remote_peer);
			}

			/* if AF_UNSPEC, all we do is dissolve and return */
			if(saddr->sin_family == AF_UNSPEC) {
				return VSOCKET_SUCCESS;
			}

			/* finally, we "connect" be setting the new default destination */
			sock->vt->vudp->default_remote_peer = vpeer_create(saddr->sin_addr.s_addr, saddr->sin_port);
		}

		return VSOCKET_SUCCESS;
	}

	return VSOCKET_ERROR;
}

int vsocket_getpeername(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t* saddr_len) {
	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* check for NULL addr */
	if(saddr == NULL) {
		errno = EFAULT;
		return VSOCKET_ERROR;
	}

	if(*saddr_len < sizeof(struct sockaddr_in)){
		errno = EINVAL;
		return VSOCKET_ERROR;
	}

	/* check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	/* check if we are connected */
	if(sock->type == SOCK_DGRAM || sock->vt->vtcp->remote_peer == NULL){
		errno = ENOTCONN;
		return VSOCKET_ERROR;
	}

	/* get the name of the peer */
	saddr->sin_addr.s_addr = sock->vt->vtcp->remote_peer->addr;
	saddr->sin_port = sock->vt->vtcp->remote_peer->port;
	*saddr_len = sizeof(struct sockaddr_in);

	return VSOCKET_SUCCESS;
}

ssize_t vsocket_send(vsocket_mgr_tp net, int fd, const void* buf, size_t n, int flags) {
	return vsocket_sendto(net, fd, buf, n, flags, NULL, 0);
}

ssize_t vsocket_recv(vsocket_mgr_tp net, int fd, void* buf, size_t n, int flags) {
	return vsocket_recvfrom(net, fd, buf, n, flags, NULL, 0);
}

ssize_t vsocket_sendto(vsocket_mgr_tp net, int fd, const void* buf, size_t n, int flags,
		struct sockaddr_in* saddr, socklen_t saddr_len) {
	/* block sending if we have yet to absorb cpu delays */
	if(vcpu_is_blocking(net->vcpu)) {
		debugf("vsocket_sendto: blocked on CPU when trying to send %lu bytes from socket %i\n", n, fd);
		errno = EAGAIN;
		return VSOCKET_ERROR;
	}


	/* TODO flags are ignored */
	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* if this is a pipe, redirect */
	enum vpipe_status stat = vpipe_stat(net->vpipe_mgr, fd);
	if(stat == VPIPE_OPEN) {
		ssize_t pwritten = vpipe_write(net->vpipe_mgr, fd, buf, n);
		if(pwritten < 0) {
			/* open but didnt write... hopefully the error popped up */
			errno = EAGAIN;
		}
		return pwritten;
	} else if(stat == VPIPE_READONLY) {
		/* we have an active pipe, but cant write */
		errno = ECONNRESET;
		return VSOCKET_ERROR;
	}

	/* not a pipe, check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	ssize_t result;

	/* socket-type-specific checks */
	if(sock->type == SOCK_STREAM){
		if(saddr != NULL || saddr_len != 0){
			/* we may ignore the recipient specification for TCP */
			/* errno = EISCONN;
			return VSOCKET_ERROR; */
		}

		if(sock->vt == NULL || sock->vt->vtcp == NULL) {
			/* internal error */
			dlogf(LOG_ERR, "vsocket_sendto: NULL transport objects");
			errno = EINVAL;
			return VSOCKET_ERROR;
		}

		if (sock->vt->vtcp->connection_was_reset) {
			errno = ECONNRESET;
			return VSOCKET_ERROR;
		}

		if(sock->curr_state == VTCP_CLOSED ||
				sock->curr_state == VTCP_CLOSING) {
			/* user initiated close.
			 * if other end already got everything we sent, we are CLOSED.
			 * if we are waiting for their status, we are CLOSING. */
			return 0;
		}

		if(sock->do_delete || sock->vt->vtcp->remote_peer == NULL
				|| sock->curr_state != VTCP_ESTABLISHED) {
			/* cant send anything anymore, only possibly read if in CLOSE_WAIT */
			errno = ENOTCONN;
			return VSOCKET_ERROR;
		}

		/* finally send, addr and addr_len are ignored for stream sockets */
		result = vtcp_send(net, sock, buf, n);
	} else {
		in_addr_t dest_addr = 0;
		in_port_t dest_port = 0;

		/* check that we have somewhere to send it */
		if(saddr != NULL) {
			dest_addr = saddr->sin_addr.s_addr;
			dest_port = saddr->sin_port;
		} else {
			/* its ok as long as they setup a default destination with connect() */
			if(sock->vt != NULL && sock->vt->vudp != NULL) {
				if(sock->vt->vudp->default_remote_peer == NULL) {
					/* we have nowhere to send it */
					errno = EDESTADDRREQ;
					return VSOCKET_ERROR;
				}
			}
		}

		/* if this socket is not bound, do an implicit bind to a random port */
		if(sock->ethernet_peer == NULL && sock->loopback_peer == NULL) {
			in_addr_t bind_addr;
			if(saddr->sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
				bind_addr = htonl(INADDR_LOOPBACK);
			} else {
				bind_addr = net->addr;
			}
			if(vsocket_bind_implicit(net, fd, bind_addr) != VSOCKET_SUCCESS) {
				return VSOCKET_ERROR;
			}
		}

		/* check if message is too large */
		if(n > VTRANSPORT_TCP_MAX_STREAM_SIZE) {
			errno = EMSGSIZE;
			return VSOCKET_ERROR;
		}

		/* finally send the data */
		result = vudp_send(net, sock, buf, n, dest_addr, dest_port);
	}

	if(result <= 0) {
		result = VSOCKET_ERROR;
		errno = EAGAIN;
	} else {
		debugf("user sent %zd bytes\n", result);

		/* user is reading some bytes. lets assume some cpu processing delay
		 * here since they will need to copy these and process them. */
		vcpu_add_load_write(net->vcpu, (uint32_t)result);
	}
	return result;
}

ssize_t vsocket_recvfrom(vsocket_mgr_tp net, int fd, void* buf, size_t n, int flags,
		struct sockaddr_in* saddr, socklen_t* saddr_len) {
	/* block receiving if we have yet to absorb cpu delays */
	if(vcpu_is_blocking(net->vcpu)) {
		debugf("vsocket_recvfrom: blocked on CPU when trying to receive from socket %i\n", fd);
		errno = EAGAIN;
		return VSOCKET_ERROR;
	}

	/* TODO flags are ignored */
	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* if this is a pipe, redirect */
	enum vpipe_status stat = vpipe_stat(net->vpipe_mgr, fd);
	if(stat == VPIPE_OPEN || stat == VPIPE_READONLY) {
		ssize_t pread = vpipe_read(net->vpipe_mgr, fd, buf, n);
		if(pread < 0) {
			/* open but didnt read... hopefully the error popped up */
			errno = EAGAIN;
		}
		return pread;
	}

	/* not a pipe, check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	ssize_t result = 0;

	/* socket-type-specific checks */
	if(sock->type == SOCK_STREAM){
		/* check that a stream socket is connected */
		if(sock->vt->vtcp == NULL && sock->vt->vtcp->connection_was_reset) {
			errno = ECONNREFUSED;
			return VSOCKET_ERROR;
		}

		if(sock->curr_state == VTCP_CLOSED ||
				sock->curr_state == VTCP_CLOSING) {
			/* only CLOSE_WAIT can still recv until EOF */
			errno = ENOTCONN;
			return VSOCKET_ERROR;

		}

		if(sock->vt->vtcp == NULL || sock->vt->vtcp->remote_peer == NULL ||
				((sock->curr_state != VTCP_ESTABLISHED) && (sock->curr_state != VTCP_CLOSE_WAIT))){
			errno = ENOTCONN;
			return VSOCKET_ERROR;
		}

		result = vtcp_recv(net, sock, buf, n);
	} else {
		in_addr_t* addr = NULL;
		in_port_t* port = NULL;
		if(saddr != NULL && saddr_len != NULL && *saddr_len >= sizeof(struct sockaddr_in)) {
			addr = &saddr->sin_addr.s_addr;
			port = &saddr->sin_port;
		}

		result = vudp_recv(net, sock, buf, n, addr, port);
	}

	if(result <= 0) {
		if(sock->vt != NULL && sock->vt->vtcp != NULL &&
				sock->curr_state == VTCP_CLOSE_WAIT &&
				sock->vt->vtcp->rcv_end <= sock->vt->vtcp->rcv_nxt) {
			/* other side said close and got everything from the network.
			 * recv buf is empty. signal EOF to user and destroy the socket.
			 */
			vsocket_mgr_try_destroy_socket(net, sock);
			result = 0;
		} else {
			result = VSOCKET_ERROR;
			errno = EAGAIN;
		}
	} else {
		debugf("user received %zd bytes\n", result);
		if(sock->curr_state == VTCP_CLOSE_WAIT) {
			/* make sure user keeps reading till EOF */
			vepoll_mark_available(sock->vep, VEPOLL_READ);
		}
	}

	if(result > 0) {
		/* user is reading some bytes. lets assume some cpu processing delay
		 * here since they will need to copy these and process them. */
		vcpu_add_load_read(net->vcpu, (uint32_t)result);
	}

	return result;
}

ssize_t vsocket_sendmsg(vsocket_mgr_tp net, int fd, const struct msghdr* message, int flags) {
	/* TODO implement */
	dlogf(LOG_WARN, "vsocket_sendmsg: sendmsg not implemented\n");
	errno = ENOSYS;
	return (ssize_t) VSOCKET_ERROR;
}

ssize_t vsocket_recvmsg(vsocket_mgr_tp net, int fd, struct msghdr* message, int flags) {
	/* TODO implement */
	dlogf(LOG_WARN, "vsocket_recvmsg: recvmsg not implemented\n");
	errno = ENOSYS;
	return (ssize_t) VSOCKET_ERROR;
}

int vsocket_getsockopt(vsocket_mgr_tp net, int fd, int level, int optname, void* optval,
		socklen_t* optlen) {
	/* TODO implement required options/levels */
	if(level == SOL_SOCKET || level == SOL_IP) {
		switch (optname) {
			case SO_ERROR:
				*((int*)optval) = 0;
				*optlen = sizeof(int);
				break;

			default:
				dlogf(LOG_WARN, "vsocket_getsockopt: option not implemented\n");
				errno = ENOSYS;
				return VSOCKET_ERROR;
		}

		return VSOCKET_SUCCESS;
	} else {
		dlogf(LOG_WARN, "vsocket_getsockopt: level not implemented\n");
		errno = ENOSYS;
		return VSOCKET_ERROR;
	}
}

int vsocket_setsockopt(vsocket_mgr_tp net, int fd, int level, int optname, const void* optval,
		socklen_t optlen) {
	/* TODO implement */
	dlogf(LOG_WARN, "vsocket_setsockopt: setsockopt not implemented\n");
	errno = ENOSYS;
	return VSOCKET_ERROR;
}

int vsocket_listen(vsocket_mgr_tp net, int fd, int backlog) {
	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	/* this must be a tcp socket */
	if(sock->type != SOCK_STREAM) {
		errno = EOPNOTSUPP;
		return VSOCKET_ERROR;
	}

	/* if not already bound, implicitly bind to default address and random port */
	if(sock->ethernet_peer == NULL && sock->loopback_peer == NULL) {
		if(vsocket_bind_implicit(net, fd, htonl(INADDR_ANY)) != VSOCKET_SUCCESS) {
			return VSOCKET_ERROR;
		}
	}

	/* all is good, we have a bound TCP socket ready to listen at an unused port */

	/* build the tcp server that will listen at our server port */
	vtcp_server_tp server = vtcp_server_create(net, sock, backlog);
	vsocket_mgr_add_server(net, server);

	/* we are now listening for connections */
	vsocket_transition(sock, VTCP_LISTEN);

	return VSOCKET_SUCCESS;
}

int vsocket_accept(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t* saddr_len) {
	/* check if this is a socket */
	if(fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);
	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	if(sock->type != SOCK_STREAM) {
		errno = EOPNOTSUPP;
		return VSOCKET_ERROR;
	}

	/* make sure we are listening and bound to a addr and port */
	if(sock->vt->vtcp == NULL ||
			sock->curr_state != VTCP_LISTEN ||
			(sock->ethernet_peer == NULL && sock->loopback_peer == NULL)) {
		errno = EINVAL;
		return VSOCKET_ERROR;
	}

	/* get our tcp server */
	vtcp_server_tp server = vsocket_mgr_get_server(net, sock);
	if(server == NULL){
		errno = EINVAL;
		return VSOCKET_ERROR;
	}

	/* if there are no pending connection ready to accept, dont block waiting */
	vtcp_server_child_tp pending_child = vtcp_server_remove_child_pending(server);
	vsocket_tp pending_sock = pending_child == NULL ? NULL : pending_child->sock;
	if(pending_sock == NULL) {
		errno = EWOULDBLOCK;
		return VSOCKET_ERROR;
	}

	/* we have a connection and socket ready, it will now be accepted
	 * make sure socket is still good */
	if(pending_sock->vt->vtcp == NULL || pending_sock->curr_state != VTCP_ESTABLISHED){
		/* close stale socket whose connection was reset before accepted */
		if(pending_sock->vt->vtcp != NULL && pending_sock->vt->vtcp->connection_was_reset) {
			vsocket_close(net, pending_sock->sock_desc);
		}
		errno = ECONNABORTED;
		return VSOCKET_ERROR;
	}

	if(pending_sock->vt->vtcp->remote_peer == NULL) {
		dlogf(LOG_ERR, "vsocket_accept: no remote peer on pending connection\n");
		errno = VSOCKET_ERROR;
		return VSOCKET_ERROR;
	}

	vtcp_server_add_child_accepted(server, pending_child);

	/* update child status */
	vepoll_mark_active(pending_sock->vep);
	vepoll_mark_available(pending_sock->vep, VEPOLL_WRITE);

	/* update server status */
	if(list_get_size(server->pending_queue) > 0) {
		vepoll_mark_available(sock->vep, VEPOLL_READ);
	} else {
		vepoll_mark_unavailable(sock->vep, VEPOLL_READ);
	}

	if(saddr != NULL && saddr_len != NULL && *saddr_len >= sizeof(struct sockaddr_in)) {
		saddr->sin_addr.s_addr = pending_sock->vt->vtcp->remote_peer->addr;
		saddr->sin_port = pending_sock->vt->vtcp->remote_peer->port;
		saddr->sin_family = PF_INET;
		*saddr_len = sizeof(*saddr);
	}

	return pending_sock->sock_desc;
}

int vsocket_shutdown(vsocket_mgr_tp net, int fd, int how) {
	/* TODO implement */
	dlogf(LOG_WARN, "vsocket_shutdown: shutdown not implemented\n");
	errno = ENOSYS;
	return VSOCKET_ERROR;
}

ssize_t vsocket_read(vsocket_mgr_tp net, int fd, void* buf, size_t n) {
	return vsocket_recvfrom(net, fd, buf, n, 0, NULL, 0);
}

ssize_t vsocket_write(vsocket_mgr_tp net, int fd, const void* buf, size_t n) {
	return vsocket_sendto(net, fd, buf, n, 0, NULL, 0);
}

int vsocket_close(vsocket_mgr_tp net, int fd) {
	/* check if this is a socket */
	if(net == NULL || fd < VNETWORK_MIN_SD){
		errno = ENOTSOCK;
		return VSOCKET_ERROR;
	}

	/* if this is a pipe, redirect */
	enum vpipe_status stat = vpipe_stat(net->vpipe_mgr, fd);
	if(stat == VPIPE_OPEN || stat == VPIPE_READONLY) {
		if(vpipe_close(net->vpipe_mgr, fd) == VPIPE_FAILURE) {
			errno = EIO;
			return VSOCKET_ERROR;
		} else {
			return VSOCKET_SUCCESS;
		}
	}

	/* not a pipe, check if the socket exists */
	vsocket_tp sock = vsocket_mgr_get_socket(net, fd);

	if(sock != NULL) {
		vepoll_mark_inactive(sock->vep);
	}

	/* marked for deletion can be considered a successful close */
	if(sock != NULL && sock->do_delete) {
		/* TODO this is the only place we try and destroy a socket that was
		 * previously not destroyed because we had to wait for its data to be drained.
		 * we need some way of being notified when a socket is empty so we
		 * can destroy it if needed
		 */
		vsocket_mgr_try_destroy_socket(net, sock);
		return VSOCKET_SUCCESS;
	}

	if(sock == NULL && net->destroyed_descs != NULL &&
			hashtable_remove(net->destroyed_descs, fd) != NULL) {
		/* socket was previously deleted, considered a successful close */
		return VSOCKET_SUCCESS;
	}

	if(sock == NULL) {
		errno = EBADF;
		return VSOCKET_ERROR;
	}

	if(sock->type == SOCK_STREAM && sock->vt != NULL && sock->vt->vtcp != NULL) {
		enum vsocket_state state = sock->curr_state;

		/* we should not accept anything else from application */
		vsocket_transition(sock, VTCP_CLOSING);

		if(state == VTCP_LISTEN && sock->vt->vtcp->remote_peer == NULL) {
			/* this is a server socket. it creates and forks new connections,
			 * but is not connected itself. when its last child is destroyed,
			 * it will also be destroyed.
			 * check if it can be destroyed now. */
			vsocket_try_destroy_server(net, sock);
		} else if(state != VTCP_CLOSED && state != VTCP_CLOSING && state != VTCP_CLOSE_WAIT
				&& sock->vt->vtcp->remote_peer != NULL) {
			/* we need to schedule a closing event for other end.
			 * they should close after receiving everything we already sent */
			if(sock->ethernet_peer != NULL) {
				vci_schedule_close(net->addr, sock->ethernet_peer->addr, sock->ethernet_peer->port,
						sock->vt->vtcp->remote_peer->addr, sock->vt->vtcp->remote_peer->port,
						sock->vt->vtcp->snd_end);
			}
			if(sock->loopback_peer != NULL) {
				vci_schedule_close(net->addr, sock->loopback_peer->addr, sock->loopback_peer->port,
						sock->vt->vtcp->remote_peer->addr, sock->vt->vtcp->remote_peer->port,
						sock->vt->vtcp->snd_end);
			}
		}
	} else {
		vsocket_mgr_destroy_and_remove_socket(net, sock);
	}

	return VSOCKET_SUCCESS;
}
