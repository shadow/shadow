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

#include <sys/socket.h>

#include "vsocket_intercept.h"
#include "vsocket.h"
#include "context.h"

/* we are intercepting calls from the module, which means we can
 * access the current context for provider information.
 */
static vsocket_mgr_tp vsocket_intercept_get_net(){
	return (vsocket_mgr_tp) global_sim_context.current_context->vsocket_mgr;
}

int intercept_socket(int domain, int type, int protocol) {
	return vsocket_socket(vsocket_intercept_get_net(), domain, type, protocol);
}

int intercept_socketpair(int domain, int type, int protocol, int fds[2]) {
	return vsocket_socketpair(vsocket_intercept_get_net(), domain, type, protocol, fds);
}

int intercept_bind(int fd, const struct sockaddr* addr, socklen_t len) {
	return vsocket_bind(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

int intercept_getsockname(int fd, struct sockaddr* addr, socklen_t* len) {
	return vsocket_getsockname(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

int intercept_connect(int fd, const struct sockaddr* addr, socklen_t len) {
	return vsocket_connect(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

int intercept_getpeername(int fd, struct sockaddr* addr, socklen_t* len) {
	return vsocket_getpeername(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

ssize_t intercept_send(int fd, const void* buf, size_t n, int flags) {
	return vsocket_send(vsocket_intercept_get_net(), fd, buf, n, flags);
}

ssize_t intercept_recv(int fd, void* buf, size_t n, int flags) {
	return vsocket_recv(vsocket_intercept_get_net(), fd, buf, n, flags);
}

ssize_t intercept_sendto(int fd, const void* buf, size_t n, int flags,
		const struct sockaddr* addr, socklen_t addr_len) {
	return vsocket_sendto(vsocket_intercept_get_net(), fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len);
}

ssize_t intercept_recvfrom(int fd, void* buf, size_t n, int flags,
		struct sockaddr* addr, socklen_t* addr_len) {
	return vsocket_recvfrom(vsocket_intercept_get_net(), fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len);
}

ssize_t intercept_sendmsg(int fd, const struct msghdr* message, int flags) {
	return vsocket_sendmsg(vsocket_intercept_get_net(), fd, message, flags);
}

ssize_t intercept_recvmsg(int fd, struct msghdr* message, int flags) {
	return vsocket_recvmsg(vsocket_intercept_get_net(), fd, message, flags);
}

int intercept_getsockopt(int fd, int level, int optname, void* optval,
		socklen_t* optlen) {
	return vsocket_getsockopt(vsocket_intercept_get_net(), fd, level, optname, optval, optlen);
}

int intercept_setsockopt(int fd, int level, int optname, const void* optval,
		socklen_t optlen) {
	return vsocket_setsockopt(vsocket_intercept_get_net(), fd, level, optname, optval, optlen);
}

int intercept_listen(int fd, int backlog) {
	return vsocket_listen(vsocket_intercept_get_net(), fd, backlog);
}

int intercept_accept(int fd, struct sockaddr* addr, socklen_t* addr_len) {
	return vsocket_accept(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, addr_len);
}

int intercept_shutdown(int fd, int how) {
	return vsocket_shutdown(vsocket_intercept_get_net(), fd, how);
}

ssize_t intercept_read(int fd, void* buf, int numbytes) {
	return vsocket_read(vsocket_intercept_get_net(), fd, buf, numbytes);
}

ssize_t intercept_write(int fd, const void* buf, int n) {
	return vsocket_write(vsocket_intercept_get_net(), fd, buf, n);
}

int intercept_close(int fd) {
	return vsocket_close(vsocket_intercept_get_net(), fd);
}
