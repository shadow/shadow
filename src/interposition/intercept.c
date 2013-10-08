/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "shadow.h"

int intercept_worker_isInShadowContext() {
	return worker_isInShadowContext();
}

/**
 * Crypto
 */

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void intercept_AES_encrypt(const guchar *in, guchar *out, const void *key) {
	return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void intercept_AES_decrypt(const guchar *in, guchar *out, const void *key) {
	return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void intercept_AES_ctr128_encrypt(const guchar *in, guchar *out, const void *key) {
	return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void intercept_AES_ctr128_decrypt(const guchar *in, guchar *out, const void *key) {
	return;
}

/*
 * EVP_CIPHER_CTX *ctx
 * The ctx parameter has been voided to avoid requiring Openssl headers
 */
gint intercept_EVP_Cipher(void *ctx, guchar *out, const guchar *in, guint inl) {
	memmove(out, in, (size_t)inl);
	return 1;
}

void intercept_RAND_seed(const void *buf, int num) {
	system_addEntropy(buf, num);
}

void intercept_RAND_add(const void *buf, int num, double entropy) {
	system_addEntropy(buf, num);
}

int intercept_RAND_poll() {
	guint32 buf = 1;
	system_addEntropy((gpointer)&buf, 4);
	return 1;
}

int intercept_RAND_bytes(unsigned char *buf, int num) {
	return system_randomBytes(buf, num);
}

int intercept_RAND_pseudo_bytes(unsigned char *buf, int num) {
	return system_randomBytes(buf, num);
}

void intercept_RAND_cleanup() {}

int intercept_RAND_status() {
	return 1;
}

static const struct {
	void* seed;
	void* bytes;
	void* cleanup;
	void* add;
	void* pseudorand;
	void* status;
} intercept_customRandMethod = {
	intercept_RAND_seed,
	intercept_RAND_bytes,
	intercept_RAND_cleanup,
	intercept_RAND_add,
	intercept_RAND_pseudo_bytes,
	intercept_RAND_status
};

const void* intercept_RAND_get_rand_method(void) {
	return (const void *)(&intercept_customRandMethod);
}

static void _intercept_cryptoLockingFunc(int mode, int n, const char *file, int line) {
	return system_cryptoLockingFunc(mode, n, file, line);
}

void* intercept_CRYPTO_get_locking_callback() {
	return (void *)(&_intercept_cryptoLockingFunc);
}

static unsigned int _intercept_cryptoIdFunc() {
	return system_cryptoIdFunc();
}

void* intercept_CRYPTO_get_id_callback() {
	return (void *)(&_intercept_cryptoIdFunc);
}

int intercept_rand() {
	return system_getRandom();
}

int intercept_rand_r(unsigned int *seedp) {
	return system_getRandom();
}

void intercept_srand(unsigned int seed) {
	return;
}

long int intercept_random() {
	return (long int)system_getRandom();
}

int intercept_random_r(struct random_data *buf, int32_t *result) {
	utility_assert(result != NULL);
	*result = (int32_t)system_getRandom();
	return 0;
}

void intercept_srandom(unsigned int seed) {
	return;
}

int intercept_srandom_r(unsigned int seed, struct random_data *buf) {
	return 0;
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

gint intercept_gettimeofday(struct timeval *tv, void *tz) {
	return system_getTimeOfDay(tv);
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

int intercept_getnameinfo(const struct sockaddr *sa, socklen_t salen,
		char *host, size_t hostlen, char *serv, size_t servlen, int flags) {
	return system_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
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

gint intercept_fcntl(int fd, int cmd, ...) {
	va_list farg;
	va_start(farg, cmd);
	gint result = system_fcntl(fd, cmd, farg);
	va_end(farg);
	return result;
}

gint intercept_ioctl(int fd, unsigned long int request, ...) {
	va_list farg;
	va_start(farg, request);
	gint result = system_ioctl(fd, request, farg);
	va_end(farg);

	if(result != 0) {
		errno = result;
		return -1;
	} else {
		return 0;
	}
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

/**
 * memory management
 */

gpointer intercept_malloc(gsize size) {
	return system_malloc(size);
}

gpointer intercept_calloc(gsize nmemb, gsize size) {
    return system_calloc(nmemb, size);
}

gpointer intercept_realloc(gpointer ptr, gsize size) {
    return system_realloc(ptr, size);
}

void intercept_free(gpointer ptr) {
	return system_free(ptr);
}

gint intercept_posix_memalign(gpointer* memptr, gsize alignment, gsize size) {
    return system_posix_memalign(memptr, alignment, size);
}

gpointer intercept_memalign(gsize blocksize, gsize bytes) {
    return system_memalign(blocksize, bytes);
}

/* aligned_alloc doesnt exist in glibc in the current LTS version of ubuntu */
#if 0
gpointer intercept_aligned_alloc(gsize alignment, gsize size) {
	return system_aligned_alloc(alignment, size);
}
#endif

gpointer intercept_valloc(gsize size) {
    return system_valloc(size);
}

gpointer intercept_pvalloc(gsize size) {
	return system_pvalloc(size);
}
