/**
 * The Shadow Simulator
 *
 * Copyright (c) 2010-2012 Rob Jansen <jansen@cs.umn.edu>
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
#include <time.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <string.h>
#include <fcntl.h>

#include "shadow.h"

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 04000
#endif
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 02000000
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 04000
#endif

enum SystemCallType {
	SCT_BIND, SCT_CONNECT, SCT_GETSOCKNAME, SCT_GETPEERNAME,
};

static Node* _system_switchInShadowContext() {
	Worker* worker = worker_getPrivate();
	if(worker->cached_plugin) {
		plugin_setShadowContext(worker->cached_plugin, TRUE);
	}
	return worker->cached_node;
}

static void _system_switchOutShadowContext(Node* node) {
	Worker* worker = worker_getPrivate();
	if(worker->cached_plugin) {
		plugin_setShadowContext(worker->cached_plugin, FALSE);
	}
}

/**
 * system interface to epoll library
 */

gint system_epollCreate(gint size) {
	/* size should be > 0, but can otherwise be completely ignored */
	if(size < 1) {
		errno = EINVAL;
		return -1;
	}

	/* switch into shadow and create the new descriptor */
	Node* node = _system_switchInShadowContext();
	gint handle = node_createDescriptor(node, DT_EPOLL);
	_system_switchOutShadowContext(node);

	return handle;
}

gint system_epollCreate1(gint flags) {
	/*
	 * the only possible flag is EPOLL_CLOEXEC, which means we should set
	 * FD_CLOEXEC on the new file descriptor. just ignore for now.
	 */
	if(flags != 0 && flags != EPOLL_CLOEXEC) {
		errno = EINVAL;
		return -1;
	}

	/* forward on to our regular create method */
	return system_epollCreate(1);
}

gint system_epollCtl(gint epollDescriptor, gint operation, gint fileDescriptor,
		struct epoll_event* event) {
	/*
	 * initial checks before passing on to node:
	 * EINVAL if fd is the same as epfd, or the requested operation op is not
	 * supported by this interface
	 */
	if(epollDescriptor == fileDescriptor) {
		errno = EINVAL;
		return -1;
	}

	/* switch into shadow and do the operation */
	Node* node = _system_switchInShadowContext();
	gint result = node_epollControl(node, epollDescriptor, operation, fileDescriptor, event);
	_system_switchOutShadowContext(node);

	/*
	 * When successful, epoll_ctl() returns zero. When an error occurs,
	 * epoll_ctl() returns -1 and errno is set appropriately.
	 */
	if(result != 0) {
		errno = result;
		return -1;
	} else {
		return 0;
	}
}

gint system_epollWait(gint epollDescriptor, struct epoll_event* eventArray,
		gint eventArrayLength, gint timeout) {
	/*
	 * EINVAL if maxevents is less than or equal to zero.
	 */
	if(eventArrayLength <= 0) {
		errno = EINVAL;
		return -1;
	}

	/* switch to shadow context and try to get events if we have any */
	Node* node = _system_switchInShadowContext();

	/*
	 * initial checks: we can't block, so timeout must be 0. anything else will
	 * cause a warning. if they seriously want to block by passing in -1, then
	 * return interrupt below only if we have no events.
	 *
	 * @note log while in shadow context to get node info in the log
	 */
	if(timeout != 0) {
		warning("Shadow does not block, so the '%i' millisecond timeout will be ignored", timeout);
	}

	gint nEvents = 0;
	gint result = node_epollGetEvents(node, epollDescriptor, eventArray,
			eventArrayLength, &nEvents);
	_system_switchOutShadowContext(node);

	/* check if there was an error */
	if(result != 0) {
		errno = result;
		return -1;
	}

	/*
	 * if we dont have any events and they are trying to block, tell them their
	 * timeout was interrupted.
	 */
	if(timeout != 0 && nEvents <= 0) {
		errno = EINTR;
		return -1;
	}

	/* the event count. zero is fine since they weren't expecting a timer. */
	return nEvents;
}

gint system_epollPWait(gint epollDescriptor, struct epoll_event* events,
		gint maxevents, gint timeout, const sigset_t* signalSet) {
	/*
	 * this is the same as system_epollWait, except it catches signals in the
	 * signal set. lets just assume we have no signals to worry about.
	 * forward to our regular wait method.
	 *
	 * @warning we dont handle signals
	 */
	return system_epollWait(epollDescriptor, events, maxevents, timeout);
}

/**
 * system interface to socket and IO library
 * @todo move input checking here
 */

gint system_socket(gint domain, gint type, gint protocol) {
	/* we only support non-blocking sockets, and require
	 * SOCK_NONBLOCK to be set immediately */
	gboolean isBlocking = FALSE;

	/* clear non-blocking flags if set to get true type */
	if(type & SOCK_NONBLOCK) {
		type = type & ~SOCK_NONBLOCK;
		isBlocking = FALSE;
	}
	if(type & SOCK_CLOEXEC) {
		type = type & ~SOCK_CLOEXEC;
		isBlocking = FALSE;
	}

	/* check inputs for what we support */
	if(isBlocking) {
		warning("we only support non-blocking sockets: please bitwise OR 'SOCK_NONBLOCK' with type flags");
		errno = EPROTONOSUPPORT;
		return -1;
	} else if (type != SOCK_STREAM && type != SOCK_DGRAM) {
		warning("unsupported socket type \"%i\", we only support SOCK_STREAM and SOCK_DGRAM", type);
		errno = EPROTONOSUPPORT;
		return -1;
	} else if(domain != AF_INET) {
		warning("trying to create socket with domain \"%i\", we only support PF_INET", domain);
		errno = EAFNOSUPPORT;
		return -1;
	}

	/* we are all set to create the socket */
	enum DescriptorType dtype = type == SOCK_STREAM ? DT_TCPSOCKET : DT_UDPSOCKET;

	Node* node = _system_switchInShadowContext();
	gint result = node_createDescriptor(node, dtype);
	_system_switchOutShadowContext(node);
	return result;
}

gint system_socketPair(gint domain, gint type, gint protocol, gint fds[2]) {
	/* create a pair of connected sockets, i.e. a bi-directional pipe */
	if(domain != AF_UNIX) {
		errno = EAFNOSUPPORT;
		return -1;
	}

	/* only support non-blocking sockets */
	gboolean isBlocking = FALSE;

	/* clear non-blocking flags if set to get true type */
	gint realType = type;
	if(realType & SOCK_NONBLOCK) {
		realType = realType & ~SOCK_NONBLOCK;
		isBlocking = FALSE;
	}
	if(realType & SOCK_CLOEXEC) {
		realType = realType & ~SOCK_CLOEXEC;
		isBlocking = FALSE;
	}

	if(realType != SOCK_STREAM) {
		errno = EPROTONOSUPPORT;
		return -1;
	}

	if(isBlocking) {
		warning("we only support non-blocking sockets: please bitwise OR 'SOCK_NONBLOCK' with type flags");
		errno = EPROTONOSUPPORT;
		return -1;
	}

	Node* node = _system_switchInShadowContext();

	gint handle = node_createDescriptor(node, DT_SOCKETPAIR);

	Channel* channel = (Channel*) node_lookupDescriptor(node, handle);
	gint linkedHandle = channel_getLinkedHandle(channel);

	_system_switchOutShadowContext(node);

	fds[0] = handle;
	fds[1] = linkedHandle;
	return 0;
}

static gint _system_addressHelper(gint fd, const struct sockaddr* addr, socklen_t* len,
		enum SystemCallType type) {
	/* check if this is a virtual socket */
	if(fd < MIN_DESCRIPTOR){
		warning("intercepted a non-virtual descriptor");
		errno = EBADF;
		return -1;
	}

	/* check for proper addr */
	if(addr == NULL) {
		errno = EFAULT;
		return -1;
	}

	if(len == NULL || *len < sizeof(struct sockaddr_in)) {
		errno = EINVAL;
		return -1;
	}

	struct sockaddr_in* saddr = (struct sockaddr_in*) addr;
	in_addr_t ip = saddr->sin_addr.s_addr;
	in_port_t port = saddr->sin_port;
	sa_family_t family = saddr->sin_family;

	/* direct to node for further checks */
	Node* node = _system_switchInShadowContext();

	gint result = EINVAL;
	switch(type) {
		case SCT_BIND: {
			result = node_bindToInterface(node, fd, ip, port);
			break;
		}

		case SCT_CONNECT: {
			result = node_connectToPeer(node, fd, ip, port, family);
			break;
		}

		case SCT_GETPEERNAME:
		case SCT_GETSOCKNAME: {
			result = type == SCT_GETPEERNAME ?
					node_getPeerName(node, fd, &(saddr->sin_addr.s_addr), &(saddr->sin_port)) :
					node_getSocketName(node, fd, &(saddr->sin_addr.s_addr), &(saddr->sin_port));

			if(result == 0) {
				saddr->sin_family = AF_INET;
				*len = sizeof(struct sockaddr_in);
			}

			break;
		}

		default: {
			error("unrecognized system call type");
			break;
		}
	}

	_system_switchOutShadowContext(node);

	/* check if there was an error */
	if(result != 0) {
		errno = result;
		return -1;
	}

	return 0;
}


gint system_accept(gint fd, struct sockaddr* addr, socklen_t* len) {
	/* check if this is a virtual socket */
	if(fd < MIN_DESCRIPTOR){
		warning("intercepted a non-virtual descriptor");
		errno = EBADF;
		return -1;
	}

	in_addr_t ip = 0;
	in_port_t port = 0;
	gint handle = 0;

	/* direct to node for further checks */
	Node* node = _system_switchInShadowContext();

	gint result = node_acceptNewPeer(node, fd, &ip, &port, &handle);

	_system_switchOutShadowContext(node);

	/* check if there was an error */
	if(result != 0) {
		errno = result;
		return -1;
	}

	if(addr != NULL && len != NULL && *len >= sizeof(struct sockaddr_in)) {
		struct sockaddr_in* ai = (struct sockaddr_in*) addr;
		ai->sin_addr.s_addr = ip;
		ai->sin_port = port;
		ai->sin_family = AF_INET;
		*len = sizeof(struct sockaddr_in);
	}

	return handle;
}

gint system_accept4(gint fd, struct sockaddr* addr, socklen_t* len, gint flags) {
	/* just ignore the flags and call accept */
	return system_accept(fd, addr, len);
}

gint system_bind(gint fd, const struct sockaddr* addr, socklen_t len) {
	return _system_addressHelper(fd, addr, &len, SCT_BIND);
}

gint system_connect(gint fd, const struct sockaddr* addr, socklen_t len) {
	return _system_addressHelper(fd, addr, &len, SCT_CONNECT);
}

gint system_getPeerName(gint fd, struct sockaddr* addr, socklen_t* len) {
	return _system_addressHelper(fd, addr, len, SCT_GETPEERNAME);
}

gint system_getSockName(gint fd, struct sockaddr* addr, socklen_t* len) {
	return _system_addressHelper(fd, addr, len, SCT_GETSOCKNAME);
}

gssize system_sendTo(gint fd, const gpointer buf, gsize n, gint flags,
		const struct sockaddr* addr, socklen_t len) {
	/* TODO flags are ignored */
	/* check if this is a socket */
	if(fd < MIN_DESCRIPTOR){
		errno = EBADF;
		return -1;
	}

	in_addr_t ip = 0;
	in_port_t port = 0;

	/* check if they specified an address to send to */
	if(addr != NULL && len >= sizeof(struct sockaddr_in)) {
		struct sockaddr_in* si = (struct sockaddr_in*) addr;
		ip = si->sin_addr.s_addr;
		port = si->sin_port;
	}

	Node* node = _system_switchInShadowContext();
	gsize bytes = 0;
	gint result = node_sendUserData(node, fd, buf, n, ip, port, &bytes);
	_system_switchOutShadowContext(node);

	if(result != 0) {
		errno = result;
		return -1;
	}
	return (gssize) bytes;
}

gssize system_send(gint fd, const gpointer buf, gsize n, gint flags) {
	return system_sendTo(fd, buf, n, flags, NULL, 0);
}

gssize system_sendMsg(gint fd, const struct msghdr* message, gint flags) {
	/* TODO implement */
	warning("sendmsg not implemented");
	errno = ENOSYS;
	return -1;
}

gssize system_write(gint fd, const gpointer buf, gint n) {
	return system_sendTo(fd, buf, n, 0, NULL, 0);
}

gssize system_recvFrom(gint fd, gpointer buf, size_t n, gint flags,
		struct sockaddr* addr, socklen_t* len) {
	/* TODO flags are ignored */
	/* check if this is a socket */
	if(fd < MIN_DESCRIPTOR){
		errno = EBADF;
		return -1;
	}

	in_addr_t ip = 0;
	in_port_t port = 0;

	Node* node = _system_switchInShadowContext();
	gsize bytes = 0;
	gint result = node_receiveUserData(node, fd, buf, n, &ip, &port, &bytes);
	_system_switchOutShadowContext(node);

	if(result != 0) {
		errno = result;
		return -1;
	}

	/* check if they wanted to know where we got the data from */
	if(addr != NULL && len != NULL && *len >= sizeof(struct sockaddr_in)) {
		struct sockaddr_in* si = (struct sockaddr_in*) addr;
		si->sin_addr.s_addr = ip;
		si->sin_port = port;
		si->sin_family = AF_INET;
		*len = sizeof(struct sockaddr_in);
	}

	return (gssize) bytes;
}

gssize system_recv(gint fd, gpointer buf, gsize n, gint flags) {
	return system_recvFrom(fd, buf, n, flags, NULL, 0);
}

gssize system_recvMsg(gint fd, struct msghdr* message, gint flags) {
	/* TODO implement */
	warning("recvmsg not implemented");
	errno = ENOSYS;
	return -1;
}

gssize system_read(gint fd, gpointer buf, gint n) {
	return system_recvFrom(fd, buf, n, 0, NULL, 0);
}

gint system_getSockOpt(gint fd, gint level, gint optname, gpointer optval,
		socklen_t* optlen) {
	/* @todo: implement socket options */
	if(level == SOL_SOCKET || level == SOL_IP) {
		switch (optname) {
			case SO_ERROR:
				*((gint*)optval) = 0;
				*optlen = sizeof(gint);
				break;

			default:
				warning("option not implemented");
				errno = ENOSYS;
				return -1;
		}

		return 0;
	} else {
		warning("socket option level not implemented");
		errno = ENOSYS;
		return -1;
	}
}

gint system_setSockOpt(gint fd, gint level, gint optname, const gpointer optval,
		socklen_t optlen) {
	Node* node = _system_switchInShadowContext();
	/* @todo: implement socket options */
	debug("setsockopt not implemented. this is probably OK, depending on usage.");
	errno = ENOSYS;
	_system_switchOutShadowContext(node);
	return -1;
}

gint system_listen(gint fd, gint backlog) {
	/* check if this is a socket */
	if(fd < MIN_DESCRIPTOR){
		errno = EBADF;
		return -1;
	}

	Node* node = _system_switchInShadowContext();
	gint result = node_listenForPeer(node, fd, backlog);
	_system_switchOutShadowContext(node);

	/* check if there was an error */
	if(result != 0) {
		errno = result;
		return -1;
	}

	return 0;
}

gint system_shutdown(gint fd, gint how) {
	warning("shutdown not implemented");
	errno = ENOSYS;
	return -1;
}

gint system_pipe(gint pipefds[2]) {
	return system_pipe2(pipefds, O_NONBLOCK);
}

gint system_pipe2(gint pipefds[2], gint flags) {
	/* we only support non-blocking sockets, and require
	 * SOCK_NONBLOCK to be set immediately */
	gboolean isBlocking = TRUE;

	/* clear non-blocking flags if set to get true type */
	if(flags & O_NONBLOCK) {
		flags = flags & ~O_NONBLOCK;
		isBlocking = FALSE;
	}
	if(flags & O_CLOEXEC) {
		flags = flags & ~O_CLOEXEC;
		isBlocking = FALSE;
	}

	/* check inputs for what we support */
	if(isBlocking) {
		warning("we only support non-blocking pipes: please bitwise OR 'O_NONBLOCK' with flags");
		errno = EINVAL;
		return -1;
	}

	Node* node = _system_switchInShadowContext();

	gint handle = node_createDescriptor(node, DT_PIPE);

	Channel* channel = (Channel*) node_lookupDescriptor(node, handle);
	gint linkedHandle = channel_getLinkedHandle(channel);

	_system_switchOutShadowContext(node);

	pipefds[0] = handle; /* reader */
	pipefds[1] = linkedHandle; /* writer */
	return 0;
}

gint system_close(gint fd) {
	/* check if this is a socket */
	if(fd < MIN_DESCRIPTOR){
		errno = EBADF;
		return -1;
	}

	Node* node = _system_switchInShadowContext();
	gint r = node_closeUser(node, fd);
	_system_switchOutShadowContext(node);
	return r;
}

gint system_fcntl(int fd, int cmd, va_list farg) {
	/* check if this is a socket */
	if(fd < MIN_DESCRIPTOR){
		errno = EBADF;
		return -1;
	}

	/* normally, the type of farg depends on the cmd */

	return 0;
}

/**
 * system util interface
 * @todo move input checking here
 */

time_t system_time(time_t* t) {
	time_t secs = (time_t) (worker_getPrivate()->clock_now / SIMTIME_ONE_SECOND);
	if(t != NULL){
		*t = secs;
	}
	return secs;
}

gint system_clockGetTime(clockid_t clk_id, struct timespec *tp) {
	if(tp == NULL) {
		errno = EFAULT;
		return -1;
	}

	SimulationTime now = worker_getPrivate()->clock_now;
	tp->tv_sec = now / SIMTIME_ONE_SECOND;
	tp->tv_nsec = now % SIMTIME_ONE_SECOND;

	return 0;
}

gint system_getTimeOfDay(struct timeval *tv) {
	SimulationTime now = worker_getPrivate()->clock_now;
	if(tv) {
		tv->tv_sec = now / SIMTIME_ONE_SECOND;
		tv->tv_usec = (now % SIMTIME_ONE_SECOND) / SIMTIME_ONE_MICROSECOND;
	}
	return 0;
}

gint system_getHostName(gchar *name, size_t len) {
	Node* node = _system_switchInShadowContext();
	gint result = 0;

//	in_addr_t ip = node_getDefaultIP(node);
//	const gchar* hostname = internetwork_resolveID(worker_getPrivate()->cached_engine->internet, (GQuark)ip);

	if(name != NULL && node != NULL) {

		/* resolve my address to a hostname */
		const gchar* sysname = node_getName(node);

		if(sysname != NULL && len > strlen(sysname)) {
			if(strncpy(name, sysname, len) != NULL) {
				result = 0;
				goto done;
			}
		}
	}
	errno = EFAULT;
	result = -1;

	done:

	_system_switchOutShadowContext(node);
	return result;
}

gint system_getAddrInfo(gchar *name, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res) {
	Node* node = _system_switchInShadowContext();

	gint result = 0;

	*res = NULL;
	if(name != NULL && node != NULL) {

		/* node may be a number-and-dots address, or a hostname. lets hope for hostname
		 * and try that first, o/w convert to the in_addr_t and do a second lookup. */
		in_addr_t address = (in_addr_t) internetwork_resolveName(worker_getInternet(), name);

		if(address == 0) {
			/* name was not in hostname format. convert to IP format and try again */
			struct in_addr inaddr;
			gint r = inet_pton(AF_INET, name, &inaddr);

			if(r == 1) {
				/* successful conversion to IP format, now find the real hostname */
				GQuark convertedIP = (GQuark) inaddr.s_addr;
				const gchar* hostname = internetwork_resolveID(worker_getInternet(), convertedIP);

				if(hostname != NULL) {
					/* got it, so convertedIP is a valid IP */
					address = (in_addr_t) convertedIP;
				} else {
					/* name not mapped by resolver... */
					result = EAI_FAIL;
					goto done;
				}
			} else if(r == 0) {
				/* not in correct form... hmmm, too bad i guess */
				result = EAI_NONAME;
				goto done;
			} else {
				/* error occured */
				result = EAI_SYSTEM;
				goto done;
			}
		}

		/* should have address now */
		struct sockaddr_in* sa = g_malloc(sizeof(struct sockaddr_in));
		/* application will expect it in network order */
		// sa->sin_addr.s_addr = (in_addr_t) htonl((guint32)(*addr));
		sa->sin_addr.s_addr = address;
		sa->sin_family = AF_INET; /* libcurl expects this to be set */

		struct addrinfo* ai_out = g_malloc(sizeof(struct addrinfo));
		ai_out->ai_addr = (struct sockaddr*) sa;
		ai_out->ai_addrlen =  sizeof(struct sockaddr_in);
		ai_out->ai_canonname = NULL;
		ai_out->ai_family = AF_INET;
		ai_out->ai_flags = 0;
		ai_out->ai_next = NULL;
		ai_out->ai_protocol = 0;
		ai_out->ai_socktype = SOCK_STREAM;

		*res = ai_out;
		result = 0;
		goto done;
	}

	errno = EINVAL;
	result = EAI_SYSTEM;

	done:
	_system_switchOutShadowContext(node);
	return result;
}

void system_freeAddrInfo(struct addrinfo *res) {
	if(res && res->ai_addr != NULL) {
		g_free(res->ai_addr);
		res->ai_addr = NULL;
		g_free(res);
	}
}

struct hostent* system_getHostByName(const gchar* name) {
	warning("gethostbyname not yet implemented");
	return NULL;
}

int system_getHostByName_r(const gchar *name,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	warning("gethostbyname_r not yet implemented");
	return -1;
}

struct hostent* system_getHostByName2(const gchar* name, gint af) {
	warning("gethostbyname2 not yet implemented");
	return NULL;
}

int system_getHostByName2_r(const gchar *name, gint af,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	warning("gethostbyname2_r not yet implemented");
	return -1;
}

struct hostent* system_getHostByAddr(const void* addr, socklen_t len, gint type) {
	warning("gethostbyaddr not yet implemented");
	return NULL;
}

int system_getHostByAddr_r(const void *addr, socklen_t len, gint type,
               struct hostent *ret, gchar *buf, gsize buflen,
               struct hostent **result, gint *h_errnop) {
	warning("gethostbyaddr_r not yet implemented");
	return -1;
}

void system_addEntropy(gconstpointer buffer, gint numBytes) {
//	Node* node = _system_switchInShadowContext();
//
//	/* the application is trying to add some entropy to OpenSSL, but we want
//	 * to make sure our experiments are repeatable, so just add bytes from our
//	 * own source.
//	 */
//	Random* random = worker_getPrivate()->random;
//	while(numBytes > 0) {
//		gint r = random_nextRandom(random);
//		RAND_seed((gconstpointer)&r, 4);
//		numBytes -= 4;
//	}
//
//	_system_switchOutShadowContext(node);
}

gint system_randomBytes(guchar* buf, gint numBytes) {
	Node* node = _system_switchInShadowContext();

	Random* random = node_getRandom(node);
	gint bytesWritten = 0;

	while(numBytes > bytesWritten) {
		gint r = random_nextInt(random);
		gint copyLength = MIN(numBytes-bytesWritten, 4);
		g_memmove(buf+bytesWritten, &r, copyLength);
		bytesWritten += copyLength;
	}

	_system_switchOutShadowContext(node);

	return 1;
}

gint system_getRandom() {
	Node* node = _system_switchInShadowContext();
	gint r = random_nextInt(node_getRandom(node));
	_system_switchOutShadowContext(node);
	return r;
}

gpointer system_malloc(gsize size) {
	Node* node = _system_switchInShadowContext();
	gpointer ptr = malloc(size);
	tracker_addAllocatedBytes(node_getTracker(node), ptr, size);
	_system_switchOutShadowContext(node);
	return ptr;
}

gpointer system_calloc(gsize nmemb, gsize size) {
	Node* node = _system_switchInShadowContext();
	gpointer ptr = calloc(nmemb, size);
	tracker_addAllocatedBytes(node_getTracker(node), ptr, size);
	_system_switchOutShadowContext(node);
	return ptr;
}

void system_free(gpointer ptr) {
	Node* node = _system_switchInShadowContext();
	free(ptr);
	tracker_removeAllocatedBytes(node_getTracker(node), ptr);
	_system_switchOutShadowContext(node);
}

/* needed for multi-threaded openssl
 * @see '$man CRYPTO_lock'
 */
void system_cryptoLockingFunc(int mode, int n, const char *file, int line) {
	Worker* worker = worker_getPrivate();
	engine_cryptoLockingFunc(worker->cached_engine, mode, n);
}

/* needed for multi-threaded openssl
 * @see '$man CRYPTO_lock'
 */
unsigned long system_cryptoIdFunc() {
	Worker* worker = worker_getPrivate();
	return ((unsigned long) (worker->thread_id));
}
