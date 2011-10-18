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

#ifndef VSOCKET_INTERCEPT_H_
#define VSOCKET_INTERCEPT_H_

#include <glib.h>
#include <sys/socket.h>

/* system interface to socket and IO library */
gint intercept_socket(gint domain, gint type, gint protocol);
gint intercept_socketpair(gint domain, gint type, gint protocol, gint fds[2]);
gint intercept_bind(gint fd, const struct sockaddr* addr, socklen_t len);
gint intercept_getsockname(gint fd, struct sockaddr* addr, socklen_t* len);
gint intercept_connect(gint fd, const struct sockaddr* addr, socklen_t len);
gint intercept_getpeername(gint fd, struct sockaddr* addr, socklen_t* len);
ssize_t intercept_send(gint fd, const gpointer buf, size_t n, gint flags);
ssize_t intercept_recv(gint fd, gpointer buf, size_t n, gint flags);
ssize_t intercept_sendto(gint fd, const gpointer buf, size_t n, gint flags,
		const struct sockaddr* addr, socklen_t addr_len);
ssize_t intercept_recvfrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* addr_len);
ssize_t intercept_sendmsg(gint fd, const struct msghdr* message, gint flags);
ssize_t intercept_recvmsg(gint fd, struct msghdr* message, gint flags);
gint intercept_getsockopt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen);
gint intercept_setsockopt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen);
gint intercept_listen(gint fd, gint backlog);
gint intercept_accept(gint fd, struct sockaddr* addr, socklen_t* addr_len);
gint intercept_accept4(gint fd, struct sockaddr* addr, socklen_t* addr_len, gint flags);
gint intercept_shutdown(gint fd, gint how);

ssize_t intercept_read(gint fd, gpointer buf, gint numbytes);
ssize_t intercept_write(gint fd, const gpointer buf, gint n);
gint intercept_close(gint fd);

#endif /* VSOCKET_INTERCEPT_H_ */
