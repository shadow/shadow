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

#include <glib.h>
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

gint intercept_socket(gint domain, gint type, gint protocol) {
	return vsocket_socket(vsocket_intercept_get_net(), domain, type, protocol);
}

gint intercept_socketpair(gint domain, gint type, gint protocol, gint fds[2]) {
	return vsocket_socketpair(vsocket_intercept_get_net(), domain, type, protocol, fds);
}

gint intercept_bind(gint fd, const struct sockaddr* addr, socklen_t len) {
	return vsocket_bind(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

gint intercept_getsockname(gint fd, struct sockaddr* addr, socklen_t* len) {
	return vsocket_getsockname(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

gint intercept_connect(gint fd, const struct sockaddr* addr, socklen_t len) {
	return vsocket_connect(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

gint intercept_getpeername(gint fd, struct sockaddr* addr, socklen_t* len) {
	return vsocket_getpeername(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, len);
}

ssize_t intercept_send(gint fd, const gpointer buf, size_t n, gint flags) {
	return vsocket_send(vsocket_intercept_get_net(), fd, buf, n, flags);
}

ssize_t intercept_recv(gint fd, gpointer buf, size_t n, gint flags) {
	return vsocket_recv(vsocket_intercept_get_net(), fd, buf, n, flags);
}

ssize_t intercept_sendto(gint fd, const gpointer buf, size_t n, gint flags,
		const struct sockaddr* addr, socklen_t addr_len) {
	return vsocket_sendto(vsocket_intercept_get_net(), fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len);
}

ssize_t intercept_recvfrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* addr_len) {
	return vsocket_recvfrom(vsocket_intercept_get_net(), fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len);
}

ssize_t intercept_sendmsg(gint fd, const struct msghdr* message, gint flags) {
	return vsocket_sendmsg(vsocket_intercept_get_net(), fd, message, flags);
}

ssize_t intercept_recvmsg(gint fd, struct msghdr* message, gint flags) {
	return vsocket_recvmsg(vsocket_intercept_get_net(), fd, message, flags);
}

gint intercept_getsockopt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen) {
	return vsocket_getsockopt(vsocket_intercept_get_net(), fd, level, optname, optval, optlen);
}

gint intercept_setsockopt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen) {
	return vsocket_setsockopt(vsocket_intercept_get_net(), fd, level, optname, optval, optlen);
}

gint intercept_listen(gint fd, gint backlog) {
	return vsocket_listen(vsocket_intercept_get_net(), fd, backlog);
}

gint intercept_accept(gint fd, struct sockaddr* addr, socklen_t* addr_len) {
	return vsocket_accept(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, addr_len);
}

gint intercept_accept4(gint fd, struct sockaddr* addr, socklen_t* addr_len, gint flags) {
        // Call the standard accpet() function which will just ignore the flags option
	return vsocket_accept(vsocket_intercept_get_net(), fd, (struct sockaddr_in *) addr, addr_len);
}
gint intercept_shutdown(gint fd, gint how) {
	return vsocket_shutdown(vsocket_intercept_get_net(), fd, how);
}

ssize_t intercept_read(gint fd, gpointer buf, gint numbytes) {
	return vsocket_read(vsocket_intercept_get_net(), fd, buf, numbytes);
}

ssize_t intercept_write(gint fd, const gpointer buf, gint n) {
	return vsocket_write(vsocket_intercept_get_net(), fd, buf, n);
}

gint intercept_close(gint fd) {
	return vsocket_close(vsocket_intercept_get_net(), fd);
}
