/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_SYSTEM_H_
#define SHD_SYSTEM_H_

#include <glib.h>
#include <time.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <unistd.h>

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
gint system_accept(gint fd, struct sockaddr* addr, socklen_t* len);
gint system_accept4(gint fd, struct sockaddr* addr, socklen_t* len, gint flags);
gint system_bind(gint fd, const struct sockaddr* addr, socklen_t len);
gint system_connect(gint fd, const struct sockaddr* addr, socklen_t len);
gint system_getPeerName(gint fd, struct sockaddr* addr, socklen_t* len);
gint system_getSockName(gint fd, struct sockaddr* addr, socklen_t* len);
gssize system_send(gint fd, const gpointer buf, gsize n, gint flags);
gssize system_recv(gint fd, gpointer buf, gsize n, gint flags);
gssize system_sendTo(gint fd, const gpointer buf, gsize n, gint flags,
		const struct sockaddr* addr, socklen_t len);
gssize system_recvFrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* len);
gssize system_sendMsg(gint fd, const struct msghdr* message, gint flags);
gssize system_recvMsg(gint fd, struct msghdr* message, gint flags);
gint system_getSockOpt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen);
gint system_setSockOpt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen);
gint system_listen(gint fd, gint backlog);
gint system_shutdown(gint fd, gint how);
gssize system_read(gint fd, gpointer buf, gsize n);
gssize system_write(gint fd, const gpointer buf, gsize n);
gint system_fcntl(int fd, int cmd, va_list farg);
gint system_ioctl(int fd, unsigned long int request, va_list farg);
gint system_close(gint fd);

gint system_fileno(FILE *osfile);
gint system_open(const gchar* pathname, gint flags, mode_t mode);
gint system_creat(const gchar *pathname, mode_t mode);
FILE* system_fopen(const gchar *path, const gchar *mode);
FILE* system_fdopen(gint fd, const gchar *mode);
gint system_fclose(FILE *fp);
gint system___fxstat (gint ver, gint fd, struct stat *buf);
gint system_fstatfs (gint fd, struct statfs *buf);

gint system_pipe(gint pipefds[2]);
gint system_pipe2(gint pipefds[2], gint flags);
time_t system_time(time_t* t);
gint system_clockGetTime(clockid_t clk_id, struct timespec *tp);
gint system_getTimeOfDay(struct timeval *tv);
gint system_getHostName(gchar *name, size_t len);
gint system_getAddrInfo(gchar *name, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res);
void system_freeAddrInfo(struct addrinfo *res);
int system_getnameinfo(const struct sockaddr *sa, socklen_t salen,
		char *host, size_t hostlen, char *serv, size_t servlen, int flags);
struct hostent* system_getHostByName(const gchar* name);
int system_getHostByName_r(const gchar *name,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop);
struct hostent* system_getHostByName2(const gchar* name, gint af);
int system_getHostByName2_r(const gchar *name, gint af,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop);
struct hostent* system_getHostByAddr(const void* addr, socklen_t len, gint type);
int system_getHostByAddr_r(const void *addr, socklen_t len, gint type,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop);

void system_addEntropy(gconstpointer buffer, gint numBytes);
gint system_randomBytes(guchar* buf, gint numBytes);
gint system_getRandom();
void system_cryptoLockingFunc(int mode, int n, const char *file, int line);
unsigned long system_cryptoIdFunc();

gpointer system_malloc(gsize size);
gpointer system_calloc(gsize nmemb, gsize size);
gpointer system_realloc(gpointer ptr, gsize size);
void system_free(gpointer ptr);
int system_posix_memalign(gpointer* memptr, gsize alignment, gsize size);
gpointer system_memalign(gsize blocksize, gsize bytes);
gpointer system_aligned_alloc(gsize alignment, gsize size);
gpointer system_valloc(gsize size);
gpointer system_pvalloc(gsize size);

#endif /* SHD_SYSTEM_H_ */
