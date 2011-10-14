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

#include <dlfcn.h>
#include <sys/socket.h>

#include "shadow.h"

#define SOCKET_LIB_PREFIX "intercept_"

/* Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in Shadow since we expect the locations of the
 * functions to be the same for all nodes.
 */
typedef int (*accept_fp)(int, struct sockaddr*, socklen_t*);
typedef int (*accept4_fp)(int, struct sockaddr*, socklen_t*, int);
typedef int (*bind_fp)(int, const struct sockaddr*, socklen_t);
typedef int (*close_fp)(int);
typedef int (*connect_fp)(int, const struct sockaddr*, socklen_t);
typedef int (*getpeername_fp)(int, struct sockaddr*, socklen_t*);
typedef int (*getsockname_fp)(int, struct sockaddr*, socklen_t*);
typedef int (*getsockopt_fp)(int, int, int, void*, socklen_t*);
typedef int (*listen_fp)(int, int);
typedef size_t (*read_fp)(int, void*, int);
typedef size_t (*recv_fp)(int, void*, size_t, int);
typedef size_t (*recvfrom_fp)(int, void*, size_t, int, struct sockaddr*, socklen_t*);
typedef size_t (*recvmsg_fp)(int, struct msghdr*, int);
typedef size_t (*send_fp)(int, const void*, size_t, int);
typedef size_t (*sendmsg_fp)(int, const struct msghdr*, int);
typedef size_t (*sendto_fp)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
typedef int (*setsockopt_fp)(int, int, int, const void*, socklen_t);
typedef int (*shutdown_fp)(int, int);
typedef int (*socket_fp)(int, int, int);
typedef int (*socketpair_fp)(int, int, int, int[]);
typedef size_t (*write_fp)(int, const void*, int);

/* save pointers to shadow libsocket functions */
static accept_fp _vsocket_accept = NULL;
static accept4_fp _vsocket_accept4 = NULL;
static bind_fp _vsocket_bind = NULL;
static close_fp _vsocket_close = NULL;
static connect_fp _vsocket_connect = NULL;
static getpeername_fp _vsocket_getpeername = NULL;
static getsockname_fp _vsocket_getsockname = NULL;
static getsockopt_fp _vsocket_getsockopt = NULL;
static listen_fp _vsocket_listen = NULL;
static read_fp _vsocket_read = NULL;
static recv_fp _vsocket_recv = NULL;
static recvfrom_fp _vsocket_recvfrom = NULL;
static recvmsg_fp _vsocket_recvmsg = NULL;
static send_fp _vsocket_send = NULL;
static sendmsg_fp _vsocket_sendmsg = NULL;
static sendto_fp _vsocket_sendto = NULL;
static setsockopt_fp _vsocket_setsockopt = NULL;
static shutdown_fp _vsocket_shutdown = NULL;
static socket_fp _vsocket_socket = NULL;
static socketpair_fp _vsocket_socketpair = NULL;
static write_fp _vsocket_write = NULL;

/* save pointers to needed system functions */
static accept_fp _accept = NULL;
static accept4_fp _accept4 = NULL;
static bind_fp _bind = NULL;
static close_fp _close = NULL;
static connect_fp _connect = NULL;
static getpeername_fp _getpeername = NULL;
static getsockname_fp _getsockname = NULL;
static getsockopt_fp _getsockopt = NULL;
static listen_fp _listen = NULL;
static read_fp _read = NULL;
static recv_fp _recv = NULL;
static recvfrom_fp _recvfrom = NULL;
static recvmsg_fp _recvmsg = NULL;
static send_fp _send = NULL;
static sendmsg_fp _sendmsg = NULL;
static sendto_fp _sendto = NULL;
static setsockopt_fp _setsockopt = NULL;
static shutdown_fp _shutdown = NULL;
static socket_fp _socket = NULL;
static socketpair_fp _socketpair = NULL;
static write_fp _write = NULL;

int socket(int domain, int type, int protocol) {
	socket_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "socket", _socket, SOCKET_LIB_PREFIX, _vsocket_socket, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(domain, type, protocol);
}

int socketpair(int domain, int type, int protocol, int fds[2]) {
	socketpair_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "socketpair", _socketpair, SOCKET_LIB_PREFIX, _vsocket_socketpair, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(domain, type, protocol, fds);
}

int bind(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)  {
	bind_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "bind", _bind, SOCKET_LIB_PREFIX, _vsocket_bind, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, addr, len);
}

int getsockname(int fd, __SOCKADDR_ARG addr,socklen_t *__restrict len)  {
	getsockname_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getsockname", _getsockname, SOCKET_LIB_PREFIX, _vsocket_getsockname, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, addr, len);
}

int connect(int fd, __CONST_SOCKADDR_ARG addr,socklen_t len)  {
	connect_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "connect", _connect, SOCKET_LIB_PREFIX, _vsocket_connect, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, addr, len);
}

int getpeername(int fd, __SOCKADDR_ARG addr,socklen_t *__restrict len)  {
	getpeername_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getpeername", _getpeername, SOCKET_LIB_PREFIX, _vsocket_getpeername, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, addr, len);
}

ssize_t send(int fd, __const void *buf, size_t n, int flags) {
	send_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "send", _send, SOCKET_LIB_PREFIX, _vsocket_send, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, buf, n, flags);
}

ssize_t recv(int fd, void *buf, size_t n, int flags) {
	recv_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "recv", _recv, SOCKET_LIB_PREFIX, _vsocket_recv, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, buf, n, flags);
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags,
		__CONST_SOCKADDR_ARG  addr,socklen_t addr_len)  {
	sendto_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "sendto", _sendto, SOCKET_LIB_PREFIX, _vsocket_sendto, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, buf, n, flags, addr, addr_len);
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags, __SOCKADDR_ARG  addr,socklen_t *restrict addr_len)  {
	recvfrom_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "recvfrom", _recvfrom, SOCKET_LIB_PREFIX, _vsocket_recvfrom, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, buf, n, flags, addr, addr_len);
}

ssize_t sendmsg(int fd, __const struct msghdr *message, int flags) {
	sendmsg_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "sendmsg", _sendmsg, SOCKET_LIB_PREFIX, _vsocket_sendmsg, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, message, flags);
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
	recvmsg_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "recvmsg", _recvmsg, SOCKET_LIB_PREFIX, _vsocket_recvmsg, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, message, flags);
}

int getsockopt(int fd, int level, int optname, void *__restrict optval,
		socklen_t *__restrict optlen) {
	getsockopt_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getsockopt", _getsockopt, SOCKET_LIB_PREFIX, _vsocket_getsockopt, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, level, optname, optval, optlen);
}

int setsockopt(int fd, int level, int optname, __const void *optval,
		socklen_t optlen) {
	setsockopt_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "setsockopt", _setsockopt, SOCKET_LIB_PREFIX, _vsocket_setsockopt, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, level, optname, optval, optlen);
}

int listen(int fd, int n) {
	listen_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "listen", _listen, SOCKET_LIB_PREFIX, _vsocket_listen, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, n);
}

int accept(int fd, __SOCKADDR_ARG  addr,socklen_t *__restrict addr_len)  {
	accept_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "accept", _accept, SOCKET_LIB_PREFIX, _vsocket_accept, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, addr, addr_len);
}

int accept4(int fd, __SOCKADDR_ARG  addr,socklen_t *__restrict addr_len, int flags)  {
	accept4_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "accept4", _accept4, SOCKET_LIB_PREFIX, _vsocket_accept4, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, addr, addr_len, flags);
}

int shutdown(int fd, int how) {
	shutdown_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "shutdown", _shutdown, SOCKET_LIB_PREFIX, _vsocket_shutdown, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, how);
}

ssize_t read(int fd, void *buff, int numbytes) {
	read_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "read", _read, SOCKET_LIB_PREFIX, _vsocket_read, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, buff, numbytes);
}

ssize_t write(int fd, const void *buff, int n) {
	write_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "write", _write, SOCKET_LIB_PREFIX, _vsocket_write, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd, buff, n);
}

int close(int fd) {
	close_fp func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "close", _close, SOCKET_LIB_PREFIX, _vsocket_close, fd >= VNETWORK_MIN_SD);
	PRELOAD_LOOKUP(func, funcName, -1);
	return func(fd);
}
