/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <sys/socket.h>
#include <sys/epoll.h>
#include <time.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

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
void intercept_AES_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
	return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void intercept_AES_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
	return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void intercept_AES_ctr128_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
	return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void intercept_AES_ctr128_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
	return;
}

/*
 * EVP_CIPHER_CTX *ctx
 * The ctx parameter has been voided to avoid requiring Openssl headers
 */
int intercept_EVP_Cipher(void *ctx, unsigned char *out, const unsigned char *in, unsigned int inl) {
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
	uint32_t buf = 1;
	system_addEntropy((void*)&buf, 4);
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

int intercept_clock_gettime(clockid_t clk_id, struct timespec *tp) {
	return system_clockGetTime(clk_id, tp);
}

int intercept_gettimeofday(struct timeval *tv, void *tz) {
	return system_getTimeOfDay(tv);
}

int intercept_gethostname(char *name, size_t len) {
	return system_getHostName(name, len);
}

int intercept_getaddrinfo(char *node, const char *service,
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



struct hostent* intercept_gethostbyname(const char* name) {
	return system_getHostByName(name);
}

int intercept_gethostbyname_r(const char *name,
               struct hostent *ret, char *buf, size_t buflen,
               struct hostent **result, int *h_errnop) {
	return system_getHostByName_r(name, ret, buf, buflen, result, h_errnop);
}

struct hostent* intercept_gethostbyname2(const char* name, int af) {
	return system_getHostByName2(name, af);
}

int intercept_gethostbyname2_r(const char *name, int af,
               struct hostent *ret, char *buf, size_t buflen,
               struct hostent **result, int *h_errnop) {
	return system_getHostByName2_r(name, af, ret, buf, buflen, result, h_errnop);
}

struct hostent* intercept_gethostbyaddr(const void* addr, socklen_t len, int type) {
	return system_getHostByAddr(addr, len, type);
}

int intercept_gethostbyaddr_r(const void *addr, socklen_t len, int type,
               struct hostent *ret, char *buf, size_t buflen,
               struct hostent **result, int *h_errnop) {
	return system_getHostByAddr_r(addr, len, type, ret, buf, buflen, result, h_errnop);
}

/**
 * System socket and IO
 */

int intercept_socket(int domain, int type, int protocol) {
	return system_socket(domain, type, protocol);
}

int intercept_socketpair(int domain, int type, int protocol, int fds[2]) {
	return system_socketPair(domain, type, protocol, fds);
}

int intercept_bind(int fd, const struct sockaddr* addr, socklen_t len) {
	return system_bind(fd, addr, len);
}

int intercept_getsockname(int fd, struct sockaddr* addr, socklen_t* len) {
	return system_getSockName(fd, addr, len);
}

int intercept_connect(int fd, const struct sockaddr* addr, socklen_t len) {
	return system_connect(fd, addr, len);
}

int intercept_getpeername(int fd, struct sockaddr* addr, socklen_t* len) {
	return system_getPeerName(fd, addr, len);
}

ssize_t intercept_send(int fd, const void* buf, size_t n, int flags) {
	return system_send(fd, buf, n, flags);
}

ssize_t intercept_recv(int fd, void* buf, size_t n, int flags) {
	return system_recv(fd, buf, n, flags);
}

ssize_t intercept_sendto(int fd, const void* buf, size_t n, int flags,
		const struct sockaddr* addr, socklen_t addr_len) {
	return system_sendTo(fd, buf, n, flags, addr, addr_len);
}

ssize_t intercept_recvfrom(int fd, void* buf, size_t n, int flags,
		struct sockaddr* addr, socklen_t* addr_len) {
	return system_recvFrom(fd, buf, n, flags, addr, addr_len);
}

ssize_t intercept_sendmsg(int fd, const struct msghdr* message, int flags) {
	return system_sendMsg(fd, message, flags);
}

ssize_t intercept_recvmsg(int fd, struct msghdr* message, int flags) {
	return system_recvMsg(fd, message, flags);
}

int intercept_getsockopt(int fd, int level, int optname, void* optval,
		socklen_t* optlen) {
	return system_getSockOpt(fd, level, optname, optval, optlen);
}

int intercept_setsockopt(int fd, int level, int optname, const void* optval,
		socklen_t optlen) {
	return system_setSockOpt(fd, level, optname, optval, optlen);
}

int intercept_listen(int fd, int backlog) {
	return system_listen(fd, backlog);
}

int intercept_accept(int fd, struct sockaddr* addr, socklen_t* addr_len) {
	return system_accept(fd, addr, addr_len);
}

int intercept_accept4(int fd, struct sockaddr* addr, socklen_t* addr_len, int flags) {
   return system_accept4(fd, addr, addr_len, flags);
}
int intercept_shutdown(int fd, int how) {
	return system_shutdown(fd, how);
}

int intercept_pipe(int pipefd[2]) {
	return system_pipe(pipefd);
}

int intercept_pipe2(int pipefd[2], int flags) {
	return system_pipe2(pipefd, flags);
}

ssize_t intercept_read(int fd, void* buf, size_t n) {
	return system_read(fd, buf, n);
}

ssize_t intercept_write(int fd, const void* buf, size_t n) {
	return system_write(fd, buf, n);
}

int intercept_close(int fd) {
	return system_close(fd);
}

int intercept_fcntl(int fd, int cmd, ...) {
    va_list farg;
    va_start(farg, cmd);
    int result = system_fcntl(fd, cmd, farg);
    va_end(farg);
    return result;
}

int intercept_ioctl(int fd, unsigned long int request, ...) {
    va_list farg;
    va_start(farg, request);
    int result = system_ioctl(fd, request, farg);
    va_end(farg);

    if(result != 0) {
        errno = result;
        return -1;
    } else {
        return 0;
    }
}

/**
 * files
 */

int intercept_fileno(FILE *stream) {
    return system_fileno(stream);
}

int intercept_open(const char *pathname, int flags, mode_t mode) {
	return system_open(pathname, flags, mode);
}

int intercept_creat(const char *pathname, mode_t mode) {
    return system_creat(pathname, mode);
}

FILE *intercept_fopen(const char *path, const char *mode) {
    return system_fopen(path, mode);
}

FILE* intercept_fdopen(int fd, const char *mode) {
    return system_fdopen(fd, mode);
}

int intercept_dup(int oldfd) {
    return system_dup(oldfd);
}

int intercept_dup2(int oldfd, int newfd) {
    return system_dup2(oldfd, newfd);
}

int intercept_dup3(int oldfd, int newfd, int flags) {
    return system_dup3(oldfd, newfd, flags);
}

int intercept_fclose(FILE *fp) {
    return system_fclose(fp);
}

int intercept___fxstat (int ver, int fd, struct stat *buf) {
    return system___fxstat(ver, fd, buf);
}

int intercept_fstatfs (int fd, struct statfs *buf) {
    return system_fstatfs(fd, buf);
}

off_t intercept_lseek(int fd, off_t offset, int whence) {
    return system_lseek(fd, offset, whence);
}

/**
 * System epoll
 */

int intercept_epoll_create(int size) {
	return system_epollCreate(size);
}

int intercept_epoll_create1(int flags) {
	return system_epollCreate1(flags);
}

int intercept_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event) {
	return system_epollCtl(epfd, op, fd, event);
}

int intercept_epoll_wait(int epfd, struct epoll_event *events,
		int maxevents, int timeout) {
	return system_epollWait(epfd, events, maxevents, timeout);
}

int intercept_epoll_pwait(int epfd, struct epoll_event *events,
			int maxevents, int timeout, const sigset_t *ss) {
	return system_epollPWait(epfd, events, maxevents, timeout, ss);
}

/**
 * memory management
 */

void* intercept_malloc(size_t size) {
	return system_malloc(size);
}

void* intercept_calloc(size_t nmemb, size_t size) {
    return system_calloc(nmemb, size);
}

void* intercept_realloc(void* ptr, size_t size) {
    return system_realloc(ptr, size);
}

void intercept_free(void* ptr) {
	return system_free(ptr);
}

int intercept_posix_memalign(void** memptr, size_t alignment, size_t size) {
    return system_posix_memalign(memptr, alignment, size);
}

void* intercept_memalign(size_t blocksize, size_t bytes) {
    return system_memalign(blocksize, bytes);
}

/* aligned_alloc doesnt exist in glibc in the current LTS version of ubuntu */
#if 0
void* intercept_aligned_alloc(size_t alignment, size_t size) {
	return system_aligned_alloc(alignment, size);
}
#endif

void* intercept_valloc(size_t size) {
    return system_valloc(size);
}

void* intercept_pvalloc(size_t size) {
	return system_pvalloc(size);
}

void* intercept_mmap(void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) {
    return system_mmap(addr, length, prot, flags, fd, offset);
}
