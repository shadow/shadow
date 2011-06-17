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

#ifndef VSOCKET_H_
#define VSOCKET_H_

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "vsocket_mgr.h"
#include "vpeer.h"
#include "vpacket_mgr.h"
#include "vpacket.h"
#include "hashtable.h"
#include "list.h"

/* starting point for 'random' ports we select [2^16 / 2] */
#define VSOCKET_MIN_RND_PORT 30000
/* max size of incomplete, un-established connection queue, taken from /proc/sys/net/ipv4/tcp_max_syn_backlog */
#define VSOCKET_MAX_SYN_BACKLOG 1024
/* Initial send sequence number */
#define VSOCKET_ISS 0

/* most socket functions return one of these two codes (while also setting errno) */
#define VSOCKET_ERROR -1
#define VSOCKET_SUCCESS 0

#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif

#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 01000000
#endif

void vsocket_mgr_destroy_and_remove_socket(vsocket_mgr_tp net, vsocket_tp sock);
void vsocket_mgr_destroy_and_remove_socket_cb(void* value, int key, void* param);
void vsocket_mgr_destroy_socket(vsocket_tp sock);
void vsocket_mgr_destroy_socket_cb(void* value, int key);
uint64_t vsocket_get_retransmit_key(rc_vpacket_pod_tp rc_packet);
unsigned int vsocket_hash(in_addr_t addr, in_port_t port);
void vsocket_transition(vsocket_tp sock, enum vsocket_state newstate);
void vsocket_try_destroy_server(vsocket_mgr_tp net, vsocket_tp server_sock);
void vsocket_mgr_try_destroy_socket(vsocket_mgr_tp net, vsocket_tp sock);

int vsocket_socket(vsocket_mgr_tp net, int domain, int type, int protocol);
int vsocket_socketpair(vsocket_mgr_tp net, int domain, int type, int protocol, int sv[2]);
int vsocket_bind(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t saddr_len);
int vsocket_getsockname(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t* saddr_len);
int vsocket_connect(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t saddr_len);
int vsocket_getpeername(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t* saddr_len);
ssize_t vsocket_send(vsocket_mgr_tp net, int fd, const void* buf, size_t n, int flags);
ssize_t vsocket_recv(vsocket_mgr_tp net, int fd, void* buf, size_t n, int flags);
ssize_t vsocket_sendto(vsocket_mgr_tp net, int fd, const void* buf, size_t n, int flags,
		struct sockaddr_in* saddr, socklen_t saddr_len);
ssize_t vsocket_recvfrom(vsocket_mgr_tp net, int fd, void* buf, size_t n, int flags,
		struct sockaddr_in* saddr, socklen_t* saddr_len);
ssize_t vsocket_sendmsg(vsocket_mgr_tp net, int fd, const struct msghdr* message, int flags);
ssize_t vsocket_recvmsg(vsocket_mgr_tp net, int fd, struct msghdr* message, int flags);
int vsocket_getsockopt(vsocket_mgr_tp net, int fd, int level, int optname, void* optval,
		socklen_t* optlen);
int vsocket_setsockopt(vsocket_mgr_tp net, int fd, int level, int optname, const void* optval,
		socklen_t optlen);
int vsocket_listen(vsocket_mgr_tp net, int fd, int backlog);
int vsocket_accept(vsocket_mgr_tp net, int fd, struct sockaddr_in* saddr, socklen_t* saddr_len);
int vsocket_shutdown(vsocket_mgr_tp net, int fd, int how);

ssize_t vsocket_read(vsocket_mgr_tp net, int fd, void* buf, size_t n);
ssize_t vsocket_write(vsocket_mgr_tp net, int fd, const void* buf, size_t n);
int vsocket_close(vsocket_mgr_tp net, int fd);

#endif /* VSOCKET_H_ */
