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

#ifndef SHD_SYSTEM_H_
#define SHD_SYSTEM_H_

#include <glib.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <time.h>
#include <stddef.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

gint system_epollCreate(gint size);
gint system_epollCreate1(gint flags);
gint system_epollCtl(gint epollDescriptor, gint operation, gint fileDescriptor,
		struct epoll_event* event);
gint system_epollWait(gint epollDescriptor, struct epoll_event* eventArray,
		gint eventArrayLength, gint timeout);
gint system_epollPWait(gint epollDescriptor, struct epoll_event* events,
		gint maxevents, gint timeout, const sigset_t* signalSet);

gint system_socket(gint domain, gint type, gint protocol);
gint system_socketPair(gint domain, gint type, gint protocol, gint fds[2]);
gint system_bind(gint fd, const struct sockaddr* addr, socklen_t len);
gint system_getSockName(gint fd, struct sockaddr* addr, socklen_t* len);
gint system_connect(gint fd, const struct sockaddr* addr, socklen_t len);
gint system_getPeerName(gint fd, struct sockaddr* addr, socklen_t* len);
gssize system_send(gint fd, const gpointer buf, gsize n, gint flags);
gssize system_recv(gint fd, gpointer buf, gsize n, gint flags);
gssize system_sendTo(gint fd, const gpointer buf, gsize n, gint flags,
		const struct sockaddr* addr, socklen_t addr_len);
gssize system_recvFrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* addr_len);
gssize system_sendMsg(gint fd, const struct msghdr* message, gint flags);
gssize system_recvMsg(gint fd, struct msghdr* message, gint flags);
gint system_getSockOpt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen);
gint system_setSockOpt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen);
gint system_listen(gint fd, gint backlog);
gint system_accept(gint fd, struct sockaddr* addr, socklen_t* addr_len);
gint system_accept4(gint fd, struct sockaddr* addr, socklen_t* addr_len, gint flags);
gint system_shutdown(gint fd, gint how);
gssize system_read(gint fd, gpointer buf, gint numbytes);
gssize system_write(gint fd, const gpointer buf, gint numbytes);
gint system_close(gint fd);

time_t system_time(time_t* t);
gint system_clockGetTime(clockid_t clk_id, struct timespec *tp);
gint system_getHostName(gchar *name, size_t len);
gint system_getAddrInfo(gchar *n, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res);
void system_freeAddrInfo(struct addrinfo *res);

#endif /* SHD_SYSTEM_H_ */
