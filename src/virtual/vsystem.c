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

#include <glib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#include "shadow.h"

time_t vsystem_time(time_t* t) {
	/* get time from shadow */
	time_t secs = (time_t) (worker_getPrivate()->clock_now / SIMTIME_ONE_SECOND);

	if(t != NULL){
		*t = secs;
	}

	return secs;
}

gint vsystem_clock_gettime(clockid_t clk_id, struct timespec *tp) {
	if(clk_id != CLOCK_REALTIME) {
		errno = EINVAL;
		return -1;
	}

	if(tp == NULL) {
		errno = EFAULT;
		return -1;
	}

	SimulationTime now = worker_getPrivate()->clock_now;
	tp->tv_sec = now / SIMTIME_ONE_SECOND;
	tp->tv_nsec = now % SIMTIME_ONE_SECOND;

	return 0;
}

gint vsystem_gethostname(gchar *name, size_t len) {
	Worker* worker = worker_getPrivate();
	Node* node = worker->cached_node;
	if(name != NULL && node != NULL) {

		/* resolve my address to a hsotname */
		const gchar* sysname = internetwork_resolveID(worker->cached_engine->internet, node->id);

		if(sysname != NULL) {
			if(strncpy(name, sysname, len) != NULL) {
				return 0;
			}
		}
	}
	errno = EFAULT;
	return -1;
}

gint vsystem_getaddrinfo(gchar *name, const gchar *service,
		const struct addrinfo *hgints, struct addrinfo **res) {
	Worker* worker = worker_getPrivate();
	Node* node = worker->cached_node;
	if(name != NULL && node != NULL) {

		/* node may be a number-and-dots address, or a hostname. lets hope for hostname
		 * and try that first, o/w convert to the in_addr_t and do a second lookup. */
		in_addr_t address = (in_addr_t) internetwork_resolveName(worker->cached_engine->internet, name);

		if(address == 0) {
			/* name was not in hostname format. convert to IP format and try again */
			struct in_addr inaddr;
			gint result = inet_pton(AF_INET, name, &inaddr);

			if(result == 1) {
				/* successful conversion to IP format, now find the real hostname */
				GQuark convertedIP = (GQuark) inaddr.s_addr;
				const gchar* hostname = internetwork_resolveID(worker->cached_engine->internet, convertedIP);

				if(hostname != NULL) {
					/* got it, so convertedIP is a valid IP */
					address = (in_addr_t) convertedIP;
				} else {
					/* name not mapped by resolver... */
					return EAI_FAIL;
				}
			} else if(result == 0) {
				/* not in correct form... hmmm, too bad i guess */
				return EAI_NONAME;
			} else {
				/* error occured */
				return EAI_SYSTEM;
			}
		}

		/* should have address now */
		struct sockaddr_in* sa = g_malloc(sizeof(struct sockaddr_in));
		/* application will expect it in network order */
		// sa->sin_addr.s_addr = (in_addr_t) htonl((guint32)(*addr));
		sa->sin_addr.s_addr = address;

		struct addrinfo* ai_out = g_malloc(sizeof(struct addrinfo));
		ai_out->ai_addr = (struct sockaddr*) sa;
		ai_out->ai_addrlen = sizeof(in_addr_t);
		ai_out->ai_canonname = NULL;
		ai_out->ai_family = AF_INET;
		ai_out->ai_flags = 0;
		ai_out->ai_next = NULL;
		ai_out->ai_protocol = 0;
		ai_out->ai_socktype = SOCK_STREAM;

		*res = ai_out;
		return 0;
	}
	errno = EINVAL;
	return EAI_SYSTEM;
}

void vsystem_freeaddrinfo(struct addrinfo *res) {
	g_free(res->ai_addr);
	g_free(res);
	return;
}

void vsystem_add_cpu_load(gdouble number_of_encryptions) {
	vsocket_mgr_tp mgr = (vsocket_mgr_tp) worker_getPrivate()->cached_node->vsocket_mgr;
	if(mgr == NULL) {
		return;
	}

	vcpu_add_load_aes(mgr->vcpu, number_of_encryptions * 16);
}
