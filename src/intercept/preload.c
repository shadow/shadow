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
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <time.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#include "preload.h"
#include "shadow.h"

typedef int (*WorkerIsInShadowContextFunc)();
static WorkerIsInShadowContextFunc _worker_isInShadowContext = NULL;

int preload_worker_isInShadowContext() {
	WorkerIsInShadowContextFunc* func = &_worker_isInShadowContext;
	PRELOAD_LOOKUP(func, "intercept_worker_isInShadowContext", 0);
	return (*func)();
}

/** Here we setup and save function pointers to the function symbols we will be
 * searching for in the library that we are preempting. We do not need to
 * register these variables in Shadow since we expect the locations of the
 * functions to be the same for all nodes.
 *
 * We save a static pointer to the shadow version, and the real system function
 * to avoid doing the dlsym lookup on every interception.
 */

/**
 * system interface to epoll library
 */

typedef int (*epoll_create_fp)(int);
static epoll_create_fp _epoll_create = NULL;
static epoll_create_fp _epoll_create_redirect = NULL;
int epoll_create(int size) {
	epoll_create_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "epoll_create", _epoll_create, INTERCEPT_PREFIX, _epoll_create_redirect, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(size);
}

typedef int (*epoll_create1_fp)(int);
static epoll_create1_fp _epoll_create1 = NULL;
static epoll_create1_fp _epoll_create1_redirect = NULL;
int epoll_create1(int flags) {
	epoll_create1_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "epoll_create1", _epoll_create1, INTERCEPT_PREFIX, _epoll_create1_redirect, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(flags);
}

typedef int (*epoll_ctl_fp)(int, int, int, struct epoll_event*);
static epoll_ctl_fp _epoll_ctl = NULL;
static epoll_ctl_fp _epoll_ctl_redirect = NULL;
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
	epoll_ctl_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "epoll_ctl", _epoll_ctl, INTERCEPT_PREFIX, _epoll_ctl_redirect, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(epfd, op, fd, event);
}

typedef int (*epoll_wait_fp)(int, struct epoll_event*, int, int);
static epoll_wait_fp _epoll_wait = NULL;
static epoll_wait_fp _epoll_wait_redirect = NULL;
int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
	epoll_wait_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "epoll_wait", _epoll_wait, INTERCEPT_PREFIX, _epoll_wait_redirect, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(epfd, events, maxevents, timeout);
}

typedef int (*epoll_pwait_fp)(int, struct epoll_event*, int, int, const sigset_t*);
static epoll_pwait_fp _epoll_pwait = NULL;
static epoll_pwait_fp _epoll_pwait_redirect = NULL;
int epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *ss) {
	epoll_pwait_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "epoll_pwait", _epoll_pwait, INTERCEPT_PREFIX, _epoll_pwait_redirect, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(epfd, events, maxevents, timeout, ss);
}

/**
 * system interface to socket and IO library
 */

typedef int (*socket_fp)(int, int, int);
static socket_fp _socket = NULL;
static socket_fp _vsocket_socket = NULL;
int socket(int domain, int type, int protocol) {
	socket_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "socket", _socket, INTERCEPT_PREFIX, _vsocket_socket, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(domain, type, protocol);
}

typedef int (*socketpair_fp)(int, int, int, int[]);
static socketpair_fp _socketpair = NULL;
static socketpair_fp _vsocket_socketpair = NULL;
int socketpair(int domain, int type, int protocol, int fds[2]) {
	socketpair_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "socketpair", _socketpair, INTERCEPT_PREFIX, _vsocket_socketpair, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(domain, type, protocol, fds);
}

typedef int (*bind_fp)(int, const struct sockaddr*, socklen_t);
static bind_fp _bind = NULL;
static bind_fp _vsocket_bind = NULL;
int bind(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)  {
	bind_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "bind", _bind, INTERCEPT_PREFIX, _vsocket_bind, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, addr, len);
}

typedef int (*getsockname_fp)(int, struct sockaddr*, socklen_t*);
static getsockname_fp _getsockname = NULL;
static getsockname_fp _vsocket_getsockname = NULL;
int getsockname(int fd, __SOCKADDR_ARG addr,socklen_t *__restrict len)  {
	getsockname_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getsockname", _getsockname, INTERCEPT_PREFIX, _vsocket_getsockname, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, addr, len);
}

typedef int (*connect_fp)(int, const struct sockaddr*, socklen_t);
static connect_fp _connect = NULL;
static connect_fp _vsocket_connect = NULL;
int connect(int fd, __CONST_SOCKADDR_ARG addr,socklen_t len)  {
	connect_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "connect", _connect, INTERCEPT_PREFIX, _vsocket_connect, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, addr, len);
}

typedef int (*getpeername_fp)(int, struct sockaddr*, socklen_t*);
static getpeername_fp _getpeername = NULL;
static getpeername_fp _vsocket_getpeername = NULL;
int getpeername(int fd, __SOCKADDR_ARG addr,socklen_t *__restrict len)  {
	getpeername_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getpeername", _getpeername, INTERCEPT_PREFIX, _vsocket_getpeername, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, addr, len);
}

typedef size_t (*send_fp)(int, const void*, size_t, int);
static send_fp _send = NULL;
static send_fp _vsocket_send = NULL;
ssize_t send(int fd, __const void *buf, size_t n, int flags) {
	send_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "send", _send, INTERCEPT_PREFIX, _vsocket_send, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, buf, n, flags);
}

typedef size_t (*sendto_fp)(int, const void*, size_t, int, const struct sockaddr*, socklen_t);
static sendto_fp _sendto = NULL;
static sendto_fp _vsocket_sendto = NULL;
ssize_t sendto(int fd, const void *buf, size_t n, int flags,
		__CONST_SOCKADDR_ARG  addr,socklen_t addr_len)  {
	sendto_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "sendto", _sendto, INTERCEPT_PREFIX, _vsocket_sendto, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, buf, n, flags, addr, addr_len);
}

typedef size_t (*sendmsg_fp)(int, const struct msghdr*, int);
static sendmsg_fp _sendmsg = NULL;
static sendmsg_fp _vsocket_sendmsg = NULL;
ssize_t sendmsg(int fd, __const struct msghdr *message, int flags) {
	sendmsg_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "sendmsg", _sendmsg, INTERCEPT_PREFIX, _vsocket_sendmsg, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, message, flags);
}

typedef size_t (*recv_fp)(int, void*, size_t, int);
static recv_fp _recv = NULL;
static recv_fp _vsocket_recv = NULL;
ssize_t recv(int fd, void *buf, size_t n, int flags) {
	recv_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "recv", _recv, INTERCEPT_PREFIX, _vsocket_recv, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, buf, n, flags);
}

typedef size_t (*recvfrom_fp)(int, void*, size_t, int, struct sockaddr*, socklen_t*);
static recvfrom_fp _recvfrom = NULL;
static recvfrom_fp _vsocket_recvfrom = NULL;
ssize_t recvfrom(int fd, void *buf, size_t n, int flags, __SOCKADDR_ARG  addr,socklen_t *restrict addr_len)  {
	recvfrom_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "recvfrom", _recvfrom, INTERCEPT_PREFIX, _vsocket_recvfrom, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, buf, n, flags, addr, addr_len);
}

typedef size_t (*recvmsg_fp)(int, struct msghdr*, int);
static recvmsg_fp _recvmsg = NULL;
static recvmsg_fp _vsocket_recvmsg = NULL;
ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
	recvmsg_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "recvmsg", _recvmsg, INTERCEPT_PREFIX, _vsocket_recvmsg, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, message, flags);
}

typedef int (*getsockopt_fp)(int, int, int, void*, socklen_t*);
static getsockopt_fp _getsockopt = NULL;
static getsockopt_fp _vsocket_getsockopt = NULL;
int getsockopt(int fd, int level, int optname, void *__restrict optval,
		socklen_t *__restrict optlen) {
	getsockopt_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getsockopt", _getsockopt, INTERCEPT_PREFIX, _vsocket_getsockopt, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, level, optname, optval, optlen);
}

typedef int (*setsockopt_fp)(int, int, int, const void*, socklen_t);
static setsockopt_fp _setsockopt = NULL;
static setsockopt_fp _vsocket_setsockopt = NULL;
int setsockopt(int fd, int level, int optname, __const void *optval,
		socklen_t optlen) {
	setsockopt_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "setsockopt", _setsockopt, INTERCEPT_PREFIX, _vsocket_setsockopt, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, level, optname, optval, optlen);
}

typedef int (*listen_fp)(int, int);
static listen_fp _listen = NULL;
static listen_fp _vsocket_listen = NULL;
int listen(int fd, int n) {
	listen_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "listen", _listen, INTERCEPT_PREFIX, _vsocket_listen, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, n);
}

typedef int (*accept_fp)(int, struct sockaddr*, socklen_t*);
static accept_fp _accept = NULL;
static accept_fp _vsocket_accept = NULL;
int accept(int fd, __SOCKADDR_ARG  addr,socklen_t *__restrict addr_len)  {
	accept_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "accept", _accept, INTERCEPT_PREFIX, _vsocket_accept, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, addr, addr_len);
}

typedef int (*accept4_fp)(int, struct sockaddr*, socklen_t*, int);
static accept4_fp _accept4 = NULL;
static accept4_fp _vsocket_accept4 = NULL;
int accept4(int fd, __SOCKADDR_ARG  addr,socklen_t *__restrict addr_len, int flags)  {
	accept4_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "accept4", _accept4, INTERCEPT_PREFIX, _vsocket_accept4, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, addr, addr_len, flags);
}

typedef int (*shutdown_fp)(int, int);
static shutdown_fp _shutdown = NULL;
static shutdown_fp _vsocket_shutdown = NULL;
int shutdown(int fd, int how) {
	shutdown_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "shutdown", _shutdown, INTERCEPT_PREFIX, _vsocket_shutdown, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, how);
}

typedef int (*pipe_fp)(int [2]);
static pipe_fp _pipe = NULL;
static pipe_fp _vsystem_pipe = NULL;
int pipe(int pipefd[2]) {
	pipe_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "pipe", _pipe, INTERCEPT_PREFIX, _vsystem_pipe, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(pipefd);
}

typedef int (*pipe2_fp)(int [2], int);
static pipe2_fp _pipe2 = NULL;
static pipe2_fp _vsystem_pipe2 = NULL;
int pipe2(int pipefd[2], int flags) {
	pipe2_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "pipe2", _pipe2, INTERCEPT_PREFIX, _vsystem_pipe2, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(pipefd, flags);
}

typedef size_t (*read_fp)(int, void*, int);
static read_fp _read = NULL;
static read_fp _vsocket_read = NULL;
ssize_t read(int fd, void *buff, int numbytes) {
	read_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "read", _read, INTERCEPT_PREFIX, _vsocket_read, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, buff, numbytes);
}

typedef size_t (*write_fp)(int, const void*, int);
static write_fp _write = NULL;
static write_fp _vsocket_write = NULL;
ssize_t write(int fd, const void *buff, int n) {
	write_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "write", _write, INTERCEPT_PREFIX, _vsocket_write, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd, buff, n);
}

typedef int (*close_fp)(int);
static close_fp _close = NULL;
static close_fp _vsocket_close = NULL;
int close(int fd) {
	close_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "close", _close, INTERCEPT_PREFIX, _vsocket_close, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(fd);
}

typedef int (*fcntl_fp)(int, int, ...);
static fcntl_fp _fcntl = NULL;
static fcntl_fp _vsocket_fcntl = NULL;
int fcntl(int fd, int cmd, ...) {
	fcntl_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "fcntl", _fcntl, INTERCEPT_PREFIX, _vsocket_fcntl, fd >= MIN_DESCRIPTOR);
	PRELOAD_LOOKUP(func, funcName, -1);

	va_list farg;
	va_start(farg, cmd);
	int result = (*func)(fd, cmd, va_arg(farg, void*));
	va_end(farg);

	return result;
}

/**
 * system util interface
 */

typedef time_t (*time_fp)(time_t*);
static time_fp _time = NULL;
static time_fp _vsystem_time = NULL;
time_t time(time_t *t)  {
	time_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "time", _time, INTERCEPT_PREFIX, _vsystem_time, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(t);
}

typedef int (*clock_gettime_fp)(clockid_t, struct timespec *);
static clock_gettime_fp _clock_gettime = NULL;
static clock_gettime_fp _vsystem_clock_gettime = NULL;
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
	clock_gettime_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "clock_gettime", _clock_gettime, INTERCEPT_PREFIX, _vsystem_clock_gettime, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(clk_id, tp);
}

typedef int (*gethostname_fp)(char*, size_t);
static gethostname_fp _gethostname = NULL;
static gethostname_fp _vsystem_gethostname = NULL;
int gethostname(char* name, size_t len) {
	gethostname_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostname", _gethostname, INTERCEPT_PREFIX, _vsystem_gethostname, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(name, len);
}

typedef int (*getaddrinfo_fp)(const char*, const char*, const struct addrinfo*, struct addrinfo**);
static getaddrinfo_fp _getaddrinfo = NULL;
static getaddrinfo_fp _vsystem_getaddrinfo = NULL;
int getaddrinfo(const char *node, const char *service,
                       const struct addrinfo *hints,
                       struct addrinfo **res) {
	getaddrinfo_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "getaddrinfo", _getaddrinfo, INTERCEPT_PREFIX, _vsystem_getaddrinfo, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(node, service, hints, res);
}

typedef int (*freeaddrinfo_fp)(struct addrinfo*);
static freeaddrinfo_fp _freeaddrinfo = NULL;
static freeaddrinfo_fp _vsystem_freeaddrinfo = NULL;
void freeaddrinfo(struct addrinfo *res) {
	freeaddrinfo_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "freeaddrinfo", _freeaddrinfo, INTERCEPT_PREFIX, _vsystem_freeaddrinfo, 1);
	PRELOAD_LOOKUP(func, funcName,);
	(*func)(res);
}

typedef struct hostent* (*gethostbyname_fp)(const gchar*);
static gethostbyname_fp _gethostbyname = NULL;
static gethostbyname_fp _vsystem_gethostbyname = NULL;
struct hostent* gethostbyname(const gchar* name) {
	gethostbyname_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostbyname", _gethostbyname, INTERCEPT_PREFIX, _vsystem_gethostbyname, 1);
	PRELOAD_LOOKUP(func, funcName, NULL);
	return (*func)(name);
}

typedef int (*gethostbyname_r_fp)(const gchar *, struct hostent *, gchar *, gsize , struct hostent **, gint *);
static gethostbyname_r_fp _gethostbyname_r = NULL;
static gethostbyname_r_fp _vsystem_gethostbyname_r = NULL;
int gethostbyname_r(const gchar *name,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	gethostbyname_r_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostbyname_r", _gethostbyname_r, INTERCEPT_PREFIX, _vsystem_gethostbyname_r, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(name, ret, buf, buflen, result, h_errnop);
}

typedef struct hostent* (*gethostbyname2_fp)(const gchar*, gint);
static gethostbyname2_fp _gethostbyname2 = NULL;
static gethostbyname2_fp _vsystem_gethostbyname2 = NULL;
struct hostent* gethostbyname2(const gchar* name, gint af) {
	gethostbyname2_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostbyname2", _gethostbyname2, INTERCEPT_PREFIX, _vsystem_gethostbyname2, 1);
	PRELOAD_LOOKUP(func, funcName, NULL);
	return (*func)(name, af);
}

typedef int (*gethostbyname2_r_fp)(const gchar *, gint, struct hostent *, gchar *, gsize , struct hostent **, gint *);
static gethostbyname2_r_fp _gethostbyname2_r = NULL;
static gethostbyname2_r_fp _vsystem_gethostbyname2_r = NULL;
int gethostbyname2_r(const gchar *name, gint af,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	gethostbyname2_r_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostbyname2_r", _gethostbyname2_r, INTERCEPT_PREFIX, _vsystem_gethostbyname2_r, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(name, af, ret, buf, buflen, result, h_errnop);
}

typedef struct hostent* (*gethostbyaddr_fp)(const void*, socklen_t, gint);
static gethostbyaddr_fp _gethostbyaddr = NULL;
static gethostbyaddr_fp _vsystem_gethostbyaddr = NULL;
struct hostent* gethostbyaddr(const void* addr, socklen_t len, gint type) {
	gethostbyaddr_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostbyaddr", _gethostbyaddr, INTERCEPT_PREFIX, _vsystem_gethostbyaddr, 1);
	PRELOAD_LOOKUP(func, funcName, NULL);
	return (*func)(addr, len, type);
}

typedef int (*gethostbyaddr_r_fp)(const void *, socklen_t, gint, struct hostent *, gchar *, gsize , struct hostent **, gint *);
static gethostbyaddr_r_fp _gethostbyaddr_r = NULL;
static gethostbyaddr_r_fp _vsystem_gethostbyaddr_r = NULL;
int gethostbyaddr_r(const void *addr, socklen_t len, gint type,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	gethostbyaddr_r_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "gethostbyaddr_r", _gethostbyaddr_r, INTERCEPT_PREFIX, _vsystem_gethostbyaddr_r, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(addr, len, type, ret, buf, buflen, result, h_errnop);
}

/**
 * crypto interface
 */

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
typedef void (*AES_encrypt_fp)(const unsigned char *, unsigned char *, const void *);
static AES_encrypt_fp _AES_encrypt = NULL;
static AES_encrypt_fp _intercept_AES_encrypt = NULL;
void AES_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
	AES_encrypt_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "AES_encrypt", _AES_encrypt, INTERCEPT_PREFIX, _intercept_AES_encrypt, 1);
	PRELOAD_LOOKUP(func, funcName,);
	(*func)(in, out, key);
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
typedef void (*AES_decrypt_fp)(const unsigned char *, unsigned char *, const void *);
static AES_decrypt_fp _AES_decrypt = NULL;
static AES_decrypt_fp _intercept_AES_decrypt = NULL;
void AES_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
	AES_decrypt_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "AES_decrypt", _AES_decrypt, INTERCEPT_PREFIX, _intercept_AES_decrypt, 1);
	PRELOAD_LOOKUP(func, funcName,);
	(*func)(in, out, key);
}

/*
 * EVP_CIPHER_CTX *ctx
 * The ctx parameter has been voided to avoid requiring Openssl headers
 */
typedef int (*EVP_Cipher_fp)(void *ctx, unsigned char *out, const unsigned char *in, unsigned int inl);
static EVP_Cipher_fp _EVP_Cipher = NULL;
static EVP_Cipher_fp _intercept_EVP_Cipher = NULL;
int EVP_Cipher(void *ctx, unsigned char *out, const unsigned char *in, unsigned int inl){
	EVP_Cipher_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "EVP_Cipher", _EVP_Cipher, INTERCEPT_PREFIX, _intercept_EVP_Cipher, 1);
	PRELOAD_LOOKUP(func, funcName, -1);
	return (*func)(ctx, out, in, inl);
}

typedef void (*RAND_seed_fp)(const void *buf,int num);
static RAND_seed_fp _RAND_seed = NULL;
static RAND_seed_fp _intercept_RAND_seed = NULL;
void RAND_seed(const void *buf,int num) {
	RAND_seed_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "RAND_seed", _RAND_seed, INTERCEPT_PREFIX, _intercept_RAND_seed, 1);
	PRELOAD_LOOKUP(func, funcName,);
	(*func)(buf, num);
}

typedef void (*RAND_add_fp)(const void *buf,int num, double entropy);
static RAND_add_fp _RAND_add = NULL;
static RAND_add_fp _intercept_RAND_add = NULL;
void RAND_add(const void *buf,int num,double entropy) {
	RAND_add_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "RAND_add", _RAND_add, INTERCEPT_PREFIX, _intercept_RAND_add, 1);
	PRELOAD_LOOKUP(func, funcName,);
	(*func)(buf, num, entropy);
}

typedef int (*RAND_poll_fp)(void);
static RAND_poll_fp _RAND_poll = NULL;
static RAND_poll_fp _intercept_RAND_poll = NULL;
int RAND_poll(void) {
	RAND_poll_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "RAND_poll", _RAND_poll, INTERCEPT_PREFIX, _intercept_RAND_poll, 1);
	PRELOAD_LOOKUP(func, funcName, 0);
	return (*func)();
}

typedef int (*RAND_bytes_fp)(unsigned char *buf, int num);
static RAND_bytes_fp _RAND_bytes = NULL;
static RAND_bytes_fp _intercept_RAND_bytes = NULL;
int RAND_bytes(unsigned char *buf, int num) {
	RAND_bytes_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "RAND_bytes", _RAND_bytes, INTERCEPT_PREFIX, _intercept_RAND_bytes, 1);
	PRELOAD_LOOKUP(func, funcName, 0);
	return (*func)(buf, num);
}

typedef int (*RAND_pseudo_bytes_fp)(unsigned char *buf, int num);
static RAND_pseudo_bytes_fp _RAND_pseudo_bytes = NULL;
static RAND_pseudo_bytes_fp _intercept_RAND_pseudo_bytes = NULL;
int RAND_pseudo_bytes(unsigned char *buf, int num) {
	RAND_pseudo_bytes_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "RAND_pseudo_bytes", _RAND_pseudo_bytes, INTERCEPT_PREFIX, _intercept_RAND_pseudo_bytes, 1);
	PRELOAD_LOOKUP(func, funcName, 0);
	return (*func)(buf, num);
}

typedef int (*rand_fp)(void);
static rand_fp _rand = NULL;
static rand_fp _intercept_rand = NULL;
int rand(void) {
	rand_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "rand", _rand, INTERCEPT_PREFIX, _intercept_rand, 1);
	PRELOAD_LOOKUP(func, funcName, 0);
	return (*func)();
}

typedef void (*srand_fp)(unsigned int seed);
static srand_fp _srand = NULL;
static srand_fp _intercept_srand = NULL;
void srand(unsigned int seed) {
	srand_fp* func;
	char* funcName;
	PRELOAD_DECIDE(func, funcName, "srand", _srand, INTERCEPT_PREFIX, _intercept_srand, 1);
	PRELOAD_LOOKUP(func, funcName,);
	return (*func)(seed);
}
