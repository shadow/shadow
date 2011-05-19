/*
 * dvn_tor.h
 *
 *  Created on: Apr 8, 2010
 *      Author: jansen
 */

#ifndef DVN_TOR_H_
#define DVN_TOR_H_

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <netinet/in.h>

enum vtor_nodetype {
	VTOR_DIRAUTH, VTOR_RELAY, VTOR_EXITRELAY, VTOR_CLIENT
};

typedef struct vtor_s {
	char v3bw_name[255];
	enum vtor_nodetype type;
	unsigned int bandwidth;
} vtor_t, *vtor_tp;

void vtor_instantiate(vtor_tp vtor, char* hostname, enum vtor_nodetype type,
		char* bandwidth, char* torrc_path, char* datadir_path, char* geoip_path);
void vtor_destroy();
void vtor_socket_readable(vtor_tp vtor, int sockd);
void vtor_socket_writable(vtor_tp vtor, int sockd);
void vtor_loopexit_cb(int unused1, void* unused2);

int intercept_tor_open_socket(int domain, int type, int protocol);
void intercept_tor_gettimeofday(struct timeval *);
void intercept_logv(int severity, uint32_t domain, const char *funcname,
     const char *format, va_list ap);
//int vtor_intercept__evdns_nameserver_add_impl(const struct sockaddr *address, socklen_t addrlen);

int intercept_spawn_func(void (*func)(void *), void *data);
void vtor_cpuworker_init(int fd);
void vtor_cpuworker_read_cb(int sockd, short ev_types, void * arg);

#endif /* DVN_TOR_H_ */
