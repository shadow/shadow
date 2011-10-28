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

#include "preload.h"
#include "vsocket_intercept.h"
#include "global.h"

#define SOCKET_LIB_PREFIX "intercept_"

/* Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in DVN since we expect the locations of the
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

/* save pointers to dvn libsocket functions */
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
	socket_fp* fp_ptr = &_vsocket_socket;
	char* f_name = SOCKET_LIB_PREFIX "socket";

	if(type & DVN_CORE_SOCKET){
		/* clear dvn DVN_CORE_SOCKET bit */
		type = type & ~DVN_CORE_SOCKET;
		/* call made from DVN core, forward to sys socket */
		fp_ptr = &_socket;
		f_name = "socket";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(domain, type, protocol);
}

int socketpair(int domain, int type, int protocol, int fds[2]) {
	socketpair_fp* fp_ptr = &_vsocket_socketpair;
	char* f_name = SOCKET_LIB_PREFIX "socketpair";

	if(type & DVN_CORE_SOCKET){
		/* clear dvn DVN_CORE_SOCKET bit */
		type = type & ~DVN_CORE_SOCKET;
		/* call made from DVN core, forward to sys socket */
		fp_ptr = &_socketpair;
		f_name = "socketpair";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(domain, type, protocol, fds);
}

int bind(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)  {
	bind_fp* fp_ptr = &_vsocket_bind;
	char* f_name = SOCKET_LIB_PREFIX "bind";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_bind;
		f_name = "bind";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, addr, len);
}

int getsockname(int fd, __SOCKADDR_ARG addr,socklen_t *__restrict len)  {
	getsockname_fp* fp_ptr = &_vsocket_getsockname;
	char* f_name = SOCKET_LIB_PREFIX "getsockname";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_getsockname;
		f_name = "getsockname";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, addr, len);
}

int connect(int fd, __CONST_SOCKADDR_ARG addr,socklen_t len)  {
	connect_fp* fp_ptr = &_vsocket_connect;
	char* f_name = SOCKET_LIB_PREFIX "connect";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_connect;
		f_name = "connect";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, addr, len);
}

int getpeername(int fd, __SOCKADDR_ARG addr,socklen_t *__restrict len)  {
	getpeername_fp* fp_ptr = &_vsocket_getpeername;
	char* f_name = SOCKET_LIB_PREFIX "getpeername";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_getpeername;
		f_name = "getpeername";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, addr, len);
}

ssize_t send(int fd, __const void *buf, size_t n, int flags) {
	send_fp* fp_ptr = &_vsocket_send;
	char* f_name = SOCKET_LIB_PREFIX "send";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_send;
		f_name = "send";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, buf, n, flags);
}

ssize_t recv(int fd, void *buf, size_t n, int flags) {
	recv_fp* fp_ptr = &_vsocket_recv;
	char* f_name = SOCKET_LIB_PREFIX "recv";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_recv;
		f_name = "recv";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, buf, n, flags);
}

ssize_t sendto(int fd, const void *buf, size_t n, int flags,
		__CONST_SOCKADDR_ARG  addr,socklen_t addr_len)  {
	sendto_fp* fp_ptr = &_vsocket_sendto;
	char* f_name = SOCKET_LIB_PREFIX "sendto";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_sendto;
		f_name = "sendto";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, buf, n, flags, addr, addr_len);
}

ssize_t recvfrom(int fd, void *buf, size_t n, int flags, __SOCKADDR_ARG  addr,socklen_t *restrict addr_len)  {
	recvfrom_fp* fp_ptr = &_vsocket_recvfrom;
	char* f_name = SOCKET_LIB_PREFIX "recvfrom";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_recvfrom;
		f_name = "recvfrom";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, buf, n, flags, addr, addr_len);
}

ssize_t sendmsg(int fd, __const struct msghdr *message, int flags) {
	sendmsg_fp* fp_ptr = &_vsocket_sendmsg;
	char* f_name = SOCKET_LIB_PREFIX "sendmsg";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_sendmsg;
		f_name = "sendmsg";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, message, flags);
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
	recvmsg_fp* fp_ptr = &_vsocket_recvmsg;
	char* f_name = SOCKET_LIB_PREFIX "recvmsg";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_recvmsg;
		f_name = "recvmsg";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, message, flags);
}

int getsockopt(int fd, int level, int optname, void *__restrict optval,
		socklen_t *__restrict optlen) {
	getsockopt_fp* fp_ptr = &_vsocket_getsockopt;
	char* f_name = SOCKET_LIB_PREFIX "getsockopt";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_getsockopt;
		f_name = "getsockopt";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, level, optname, optval, optlen);
}

int setsockopt(int fd, int level, int optname, __const void *optval,
		socklen_t optlen) {
	setsockopt_fp* fp_ptr = &_vsocket_setsockopt;
	char* f_name = SOCKET_LIB_PREFIX "setsockopt";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_setsockopt;
		f_name = "setsockopt";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, level, optname, optval, optlen);
}

int listen(int fd, int n) {
	listen_fp* fp_ptr = &_vsocket_listen;
	char* f_name = SOCKET_LIB_PREFIX "listen";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_listen;
		f_name = "listen";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, n);
}

int accept(int fd, __SOCKADDR_ARG  addr,socklen_t *__restrict addr_len)  {
	accept_fp* fp_ptr = &_vsocket_accept;
	char* f_name = SOCKET_LIB_PREFIX "accept";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_accept;
		f_name = "accept";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, addr, addr_len);
}

int accept4(int fd, __SOCKADDR_ARG  addr,socklen_t *__restrict addr_len, int flags)  {
	accept4_fp* fp_ptr = &_vsocket_accept4;
	char* f_name = SOCKET_LIB_PREFIX "accept4";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_accept4;
		f_name = "accept4";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, addr, addr_len, flags);
}

int shutdown(int fd, int how) {
	shutdown_fp* fp_ptr = &_vsocket_shutdown;
	char* f_name = SOCKET_LIB_PREFIX "shutdown";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_shutdown;
		f_name = "shutdown";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, how);
}

ssize_t read(int fd, void *buff, int numbytes) {
	read_fp* fp_ptr = &_vsocket_read;
	char* f_name = SOCKET_LIB_PREFIX "read";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_read;
		f_name = "read";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, buff, numbytes);
}

ssize_t write(int fd, const void *buff, int n) {
	write_fp* fp_ptr = &_vsocket_write;
	char* f_name = SOCKET_LIB_PREFIX "write";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_write;
		f_name = "write";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd, buff, n);
}

int close(int fd) {
	close_fp* fp_ptr = &_vsocket_close;
	char* f_name = SOCKET_LIB_PREFIX "close";

	/* should we be forwarding to the system call? */
	if(fd < VNETWORK_MIN_SD){
		fp_ptr = &_close;
		f_name = "close";
	}

	PRELOAD_LOOKUP(fp_ptr, f_name, -1);
	return (*fp_ptr)(fd);
}
