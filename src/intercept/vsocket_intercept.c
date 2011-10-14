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

#include "shadow.h"

gint intercept_socket(gint domain, gint type, gint protocol) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_socket(mgr, domain, type, protocol),
			return r);
}

gint intercept_socketpair(gint domain, gint type, gint protocol, gint fds[2]) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_socketpair(mgr, domain, type, protocol, fds),
			return r);
}

gint intercept_bind(gint fd, const struct sockaddr* addr, socklen_t len) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_bind(mgr, fd, (struct sockaddr_in *) addr, len),
			return r);
}

gint intercept_getsockname(gint fd, struct sockaddr* addr, socklen_t* len) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_getsockname(mgr, fd, (struct sockaddr_in *) addr, len),
			return r);
}

gint intercept_connect(gint fd, const struct sockaddr* addr, socklen_t len) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_connect(mgr, fd, (struct sockaddr_in *) addr, len),
			return r);
}

gint intercept_getpeername(gint fd, struct sockaddr* addr, socklen_t* len) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_getpeername(mgr, fd, (struct sockaddr_in *) addr, len),
			return r);
}

ssize_t intercept_send(gint fd, const gpointer buf, size_t n, gint flags) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_send(mgr, fd, buf, n, flags),
			return r);
}

ssize_t intercept_recv(gint fd, gpointer buf, size_t n, gint flags) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_recv(mgr, fd, buf, n, flags),
			return r);
}

ssize_t intercept_sendto(gint fd, const gpointer buf, size_t n, gint flags,
		const struct sockaddr* addr, socklen_t addr_len) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_sendto(mgr, fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len),
			return r);
}

ssize_t intercept_recvfrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* addr_len) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_recvfrom(mgr, fd, buf, n, flags, (struct sockaddr_in *) addr, addr_len),
			return r);
}

ssize_t intercept_sendmsg(gint fd, const struct msghdr* message, gint flags) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_sendmsg(mgr, fd, message, flags),
			return r);
}

ssize_t intercept_recvmsg(gint fd, struct msghdr* message, gint flags) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_recvmsg(mgr, fd, message, flags),
			return r);
}

gint intercept_getsockopt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_getsockopt(mgr, fd, level, optname, optval, optlen),
			return r);
}

gint intercept_setsockopt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_setsockopt(mgr, fd, level, optname, optval, optlen),
			return r);
}

gint intercept_listen(gint fd, gint backlog) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_listen(mgr, fd, backlog),
			return r);
}

gint intercept_accept(gint fd, struct sockaddr* addr, socklen_t* addr_len) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_accept(mgr, fd, (struct sockaddr_in *) addr, addr_len),
			return r);
}

gint intercept_accept4(gint fd, struct sockaddr* addr, socklen_t* addr_len, gint flags) {
        // Call the standard accpet() function which will just ignore the flags option
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_accept(mgr, fd, (struct sockaddr_in *) addr, addr_len),
			return r);
}
gint intercept_shutdown(gint fd, gint how) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_shutdown(mgr, fd, how),
			return r);
}

ssize_t intercept_read(gint fd, gpointer buf, gint numbytes) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_read(mgr, fd, buf, numbytes),
			return r);
}

ssize_t intercept_write(gint fd, const gpointer buf, gint n) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			ssize_t r = vsocket_write(mgr, fd, buf, n),
			return r);
}

gint intercept_close(gint fd) {
	INTERCEPT_CONTEXT_SWITCH(vsocket_mgr_tp mgr = w->cached_node->vsocket_mgr,
			gint r = vsocket_close(mgr, fd),
			return r);
}
