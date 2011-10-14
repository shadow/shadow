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

#include <glib.h>
#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "shadow.h"

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
void vsocket_mgr_destroy_and_remove_socket_cb(gpointer key, gpointer value, gpointer param);
void vsocket_mgr_destroy_socket(vsocket_tp sock);
void vsocket_mgr_destroy_socket_cb(gpointer key, gpointer value, gpointer param);
guint64 vsocket_get_retransmit_key(rc_vpacket_pod_tp rc_packet);
guint vsocket_hash(in_addr_t addr, in_port_t port);
void vsocket_transition(vsocket_tp sock, enum vsocket_state newstate);
void vsocket_try_destroy_server(vsocket_mgr_tp net, vsocket_tp server_sock);
void vsocket_mgr_try_destroy_socket(vsocket_mgr_tp net, vsocket_tp sock);

gint vsocket_socket(vsocket_mgr_tp net, gint domain, gint type, gint protocol);
gint vsocket_socketpair(vsocket_mgr_tp net, gint domain, gint type, gint protocol, gint sv[2]);
gint vsocket_bind(vsocket_mgr_tp net, gint fd, struct sockaddr_in* saddr, socklen_t saddr_len);
gint vsocket_getsockname(vsocket_mgr_tp net, gint fd, struct sockaddr_in* saddr, socklen_t* saddr_len);
gint vsocket_connect(vsocket_mgr_tp net, gint fd, struct sockaddr_in* saddr, socklen_t saddr_len);
gint vsocket_getpeername(vsocket_mgr_tp net, gint fd, struct sockaddr_in* saddr, socklen_t* saddr_len);
ssize_t vsocket_send(vsocket_mgr_tp net, gint fd, const gpointer buf, size_t n, gint flags);
ssize_t vsocket_recv(vsocket_mgr_tp net, gint fd, gpointer buf, size_t n, gint flags);
ssize_t vsocket_sendto(vsocket_mgr_tp net, gint fd, const gpointer buf, size_t n, gint flags,
		struct sockaddr_in* saddr, socklen_t saddr_len);
ssize_t vsocket_recvfrom(vsocket_mgr_tp net, gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr_in* saddr, socklen_t* saddr_len);
ssize_t vsocket_sendmsg(vsocket_mgr_tp net, gint fd, const struct msghdr* message, gint flags);
ssize_t vsocket_recvmsg(vsocket_mgr_tp net, gint fd, struct msghdr* message, gint flags);
gint vsocket_getsockopt(vsocket_mgr_tp net, gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen);
gint vsocket_setsockopt(vsocket_mgr_tp net, gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen);
gint vsocket_listen(vsocket_mgr_tp net, gint fd, gint backlog);
gint vsocket_accept(vsocket_mgr_tp net, gint fd, struct sockaddr_in* saddr, socklen_t* saddr_len);
gint vsocket_shutdown(vsocket_mgr_tp net, gint fd, gint how);

ssize_t vsocket_read(vsocket_mgr_tp net, gint fd, gpointer buf, size_t n);
ssize_t vsocket_write(vsocket_mgr_tp net, gint fd, const gpointer buf, size_t n);
gint vsocket_close(vsocket_mgr_tp net, gint fd);

#endif /* VSOCKET_H_ */
