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

#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>

#include "vsocket_mgr.h"
#include "vtransport_mgr.h"
#include "vsystem.h"
#include "context.h"
#include "sim.h"
#include "resolver.h"
#include "vcpu.h"

time_t vsystem_time(time_t* t) {
	/* get time from dvn */
	time_t secs = (time_t) global_sim_context.sim_worker->current_time / 1000;

	if(t != NULL){
		*t = secs;
	}

	return secs;
}

int vsystem_clock_gettime(clockid_t clk_id, struct timespec *tp) {
	if(clk_id != CLOCK_REALTIME) {
		errno = EINVAL;
		return -1;
	}

	if(tp == NULL) {
		errno = EFAULT;
		return -1;
	}

	tp->tv_sec = global_sim_context.sim_worker->current_time / 1000;
	tp->tv_nsec = (global_sim_context.sim_worker->current_time % 1000) * 1000000;

	return 0;
}

int vsystem_gethostname(char *name, size_t len) {
	if(name != NULL && global_sim_context.current_context != NULL &&
			global_sim_context.current_context->vsocket_mgr != NULL &&
			global_sim_context.sim_worker != NULL) {

		/* resolve my address to a hsotname */
		in_addr_t addr = global_sim_context.current_context->vsocket_mgr->addr;
		char* sysname = resolver_resolve_byaddr(global_sim_context.sim_worker->resolver, addr);

		if(sysname != NULL) {
			if(strncpy(name, sysname, len) != NULL) {
				return 0;
			}
		}
	}
	errno = EFAULT;
	return -1;
}

int vsystem_getaddrinfo(char *node, const char *service,
		const struct addrinfo *hints, struct addrinfo **res) {
	if(node != NULL && global_sim_context.sim_worker != NULL) {
		resolver_tp r = global_sim_context.sim_worker->resolver;

		/* node may be a number-and-dots address, or a hostname. lets hope for hostname
		 * and try that first, o/w convert to the in_addr_t and do a second lookup. */
		in_addr_t* addr = resolver_resolve_byname(r, node);

		if(addr == NULL) {
			struct in_addr inaddr;
			/* convert and try again */

			int result = inet_pton(AF_INET, node, &inaddr);

			if(result == 1) {
				/* successful conversion, do lookup */
				char* hostname = resolver_resolve_byaddr(r, inaddr.s_addr);

				if(hostname != NULL) {
					/* got it, so the converted addr is valid */
					addr = resolver_resolve_byname(r, hostname);
				} else {
					/* node not mapped by resolver... */
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
		struct sockaddr_in* sa = malloc(sizeof(struct sockaddr_in));
		/* application will expect it in network order */
		// sa->sin_addr.s_addr = (in_addr_t) htonl((uint32_t)(*addr));
		sa->sin_addr.s_addr = *addr;

		struct addrinfo* ai_out = malloc(sizeof(struct addrinfo));
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
	free(res->ai_addr);
	free(res);
	return;
}

void vsystem_add_cpu_load(double number_of_encryptions) {
	vsocket_mgr_tp mgr = (vsocket_mgr_tp) global_sim_context.current_context->vsocket_mgr;
	if(mgr == NULL) {
		return;
	}

	vcpu_add_load_aes(mgr->vcpu, number_of_encryptions * 16);
}
