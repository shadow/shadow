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

#ifndef VSOCKET_INTERCEPT_H_
#define VSOCKET_INTERCEPT_H_

#include <sys/socket.h>

/* system interface to socket and IO library */
int intercept_socket(int domain, int type, int protocol);
int intercept_socketpair(int domain, int type, int protocol, int fds[2]);
int intercept_bind(int fd, const struct sockaddr* addr, socklen_t len);
int intercept_getsockname(int fd, struct sockaddr* addr, socklen_t* len);
int intercept_connect(int fd, const struct sockaddr* addr, socklen_t len);
int intercept_getpeername(int fd, struct sockaddr* addr, socklen_t* len);
ssize_t intercept_send(int fd, const void* buf, size_t n, int flags);
ssize_t intercept_recv(int fd, void* buf, size_t n, int flags);
ssize_t intercept_sendto(int fd, const void* buf, size_t n, int flags,
		const struct sockaddr* addr, socklen_t addr_len);
ssize_t intercept_recvfrom(int fd, void* buf, size_t n, int flags,
		struct sockaddr* addr, socklen_t* addr_len);
ssize_t intercept_sendmsg(int fd, const struct msghdr* message, int flags);
ssize_t intercept_recvmsg(int fd, struct msghdr* message, int flags);
int intercept_getsockopt(int fd, int level, int optname, void* optval,
		socklen_t* optlen);
int intercept_setsockopt(int fd, int level, int optname, const void* optval,
		socklen_t optlen);
int intercept_listen(int fd, int backlog);
int intercept_accept(int fd, struct sockaddr* addr, socklen_t* addr_len);
int intercept_shutdown(int fd, int how);

ssize_t intercept_read(int fd, void* buf, int numbytes);
ssize_t intercept_write(int fd, const void* buf, int n);
int intercept_close(int fd);

#endif /* VSOCKET_INTERCEPT_H_ */
