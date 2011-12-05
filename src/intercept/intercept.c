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

#include <glib.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <time.h>
#include <stddef.h>
#include <string.h>

#include "shadow.h"

int intercept_worker_isInShadowContext() {
	return worker_isInShadowContext();
}

/**
 * Crypto
 */

void intercept_AES_encrypt(const guchar *in, guchar *out, const AES_KEY *key) {
	return;
}

void intercept_AES_decrypt(const guchar *in, guchar *out, const AES_KEY *key) {
	return;
}

gint intercept_EVP_Cipher(EVP_CIPHER_CTX *ctx, guchar *out, const guchar *in, guint inl) {
	memmove(out, in, (size_t)inl);
	return 1;
}

/**
 * System utils
 */

time_t intercept_time(time_t* t) {
	return system_time(t);
}

gint intercept_clock_gettime(clockid_t clk_id, struct timespec *tp) {
	return system_clockGetTime(clk_id, tp);
}

gint intercept_gethostname(gchar *name, size_t len) {
	return system_getHostName(name, len);
}

gint intercept_getaddrinfo(gchar *node, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res) {
	return system_getAddrInfo(node, service, hgints, res);
}

void intercept_freeaddrinfo(struct addrinfo *res) {
	system_freeAddrInfo(res);
}

struct hostent* intercept_gethostbyname(const gchar* name) {
	return system_getHostByName(name);
}

int intercept_gethostbyname_r(const gchar *name,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	return system_getHostByName_r(name, ret, buf, buflen, result, h_errnop);
}

struct hostent* intercept_gethostbyname2(const gchar* name, gint af) {
	return system_getHostByName2(name, af);
}

int intercept_gethostbyname2_r(const gchar *name, gint af,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	return system_getHostByName2_r(name, af, ret, buf, buflen, result, h_errnop);
}

struct hostent* intercept_gethostbyaddr(const void* addr, socklen_t len, gint type) {
	return system_getHostByAddr(addr, len, type);
}

int intercept_gethostbyaddr_r(const void *addr, socklen_t len, gint type,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	return system_getHostByAddr_r(addr, len, type, ret, buf, buflen, result, h_errnop);
}

/**
 * System socket and IO
 */

gint intercept_socket(gint domain, gint type, gint protocol) {
	return system_socket(domain, type, protocol);
}

gint intercept_socketpair(gint domain, gint type, gint protocol, gint fds[2]) {
	return system_socketPair(domain, type, protocol, fds);
}

gint intercept_bind(gint fd, const struct sockaddr* addr, socklen_t len) {
	return system_bind(fd, addr, len);
}

gint intercept_getsockname(gint fd, struct sockaddr* addr, socklen_t* len) {
	return system_getSockName(fd, addr, len);
}

gint intercept_connect(gint fd, const struct sockaddr* addr, socklen_t len) {
	return system_connect(fd, addr, len);
}

gint intercept_getpeername(gint fd, struct sockaddr* addr, socklen_t* len) {
	return system_getPeerName(fd, addr, len);
}

ssize_t intercept_send(gint fd, const gpointer buf, size_t n, gint flags) {
	return system_send(fd, buf, n, flags);
}

ssize_t intercept_recv(gint fd, gpointer buf, size_t n, gint flags) {
	return system_recv(fd, buf, n, flags);
}

ssize_t intercept_sendto(gint fd, const gpointer buf, size_t n, gint flags,
		const struct sockaddr* addr, socklen_t addr_len) {
	return system_sendTo(fd, buf, n, flags, addr, addr_len);
}

ssize_t intercept_recvfrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* addr_len) {
	return system_recvFrom(fd, buf, n, flags, addr, addr_len);
}

ssize_t intercept_sendmsg(gint fd, const struct msghdr* message, gint flags) {
	return system_sendMsg(fd, message, flags);
}

ssize_t intercept_recvmsg(gint fd, struct msghdr* message, gint flags) {
	return system_recvMsg(fd, message, flags);
}

gint intercept_getsockopt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen) {
	return system_getSockOpt(fd, level, optname, optval, optlen);
}

gint intercept_setsockopt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen) {
	return system_setSockOpt(fd, level, optname, optval, optlen);
}

gint intercept_listen(gint fd, gint backlog) {
	return system_listen(fd, backlog);
}

gint intercept_accept(gint fd, struct sockaddr* addr, socklen_t* addr_len) {
	return system_accept(fd, addr, addr_len);
}

gint intercept_accept4(gint fd, struct sockaddr* addr, socklen_t* addr_len, gint flags) {
   return system_accept4(fd, addr, addr_len, flags);
}
gint intercept_shutdown(gint fd, gint how) {
	return system_shutdown(fd, how);
}

int intercept_pipe(int pipefd[2]) {
	return system_pipe(pipefd);
}

int intercept_pipe2(int pipefd[2], int flags) {
	return system_pipe2(pipefd, flags);
}

ssize_t intercept_read(gint fd, gpointer buf, gint n) {
	return system_read(fd, buf, n);
}

ssize_t intercept_write(gint fd, const gpointer buf, gint n) {
	return system_write(fd, buf, n);
}

gint intercept_close(gint fd) {
	return system_close(fd);
}

/**
 * System epoll
 */

gint intercept_epoll_create(gint size) {
	return system_epollCreate(size);
}

gint intercept_epoll_create1(int flags) {
	return system_epollCreate1(flags);
}

gint intercept_epoll_ctl(gint epfd, gint op, gint fd, struct epoll_event *event) {
	return system_epollCtl(epfd, op, fd, event);
}

gint intercept_epoll_wait(gint epfd, struct epoll_event *events,
		gint maxevents, gint timeout) {
	return system_epollWait(epfd, events, maxevents, timeout);
}

gint intercept_epoll_pwait(gint epfd, struct epoll_event *events,
			gint maxevents, gint timeout, const sigset_t *ss) {
	return system_epollPWait(epfd, events, maxevents, timeout, ss);
}
