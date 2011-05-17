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

#include <setjmp.h>
#include <string.h>

#include "snricall.h"
#include "snricall_codes.h"
#include "context.h"
#include "sim.h"
#include "module.h"
#include "vci.h"
#include "timer.h"
#include "vsocket_mgr.h"
#include "vsocket.h"
#include "vepoll.h"
#include "sysconfig.h"
#include "vevent_mgr.h"

static int snricall_getip(va_list va) {
	va_list vac;
	int rv;
	in_addr_t * ip_addr;

	va_copy(vac, va);
	ip_addr = va_arg(vac, in_addr_t*);

	if(ip_addr) {
		*ip_addr = global_sim_context.current_context->vsocket_mgr->addr;
		rv = SNRICALL_SUCCESS;
	} else
		rv = SNRICALL_ERROR;

	va_end(vac);
	return rv;
}

/**
 * creates a timer
 * unsigned int	- number of milliseconds from now the timer should expire
 * void (*t)(int timerid) - timer callback - function called when timer expires
 * void *		- timer callback argument
 *
 * int* 		- output/timer identifier
 */
static int snricall_create_timer(va_list va) {
	int rv;
	int *timer_id_p, timer_id;
	unsigned int delay;
	void * cb_arg;
	dtimer_ontimer_cb_fp cb;

	va_list vac;
	va_copy(vac, va);
	delay = va_arg(vac, unsigned int);
	cb = va_arg(vac,dtimer_ontimer_cb_fp);
	cb_arg = va_arg(vac,void*);
	timer_id_p = va_arg(vac, int*);
	va_end(vac);

	timer_id = dtimer_create_timer(global_sim_context.sim_worker->timer_mgr, global_sim_context.sim_worker->current_time, global_sim_context.current_context, delay, cb, cb_arg);
	if(timer_id != TIMER_INVALID && timer_id_p) {
		*timer_id_p = timer_id;
		rv = SNRICALL_SUCCESS;
	} else {
		rv = SNRICALL_ERROR;
	}

	return rv;
}

static int snricall_destroy_timer(va_list va) {
	int timer_id;

	va_list vac;
	va_copy(vac, va);
	timer_id = va_arg(vac, int);
	va_end(vac);

	dtimer_destroy_timer(global_sim_context.sim_worker->timer_mgr, global_sim_context.current_context, timer_id);

	return SNRICALL_SUCCESS;
}

static int snricall_exit(va_list va) {
	if(global_sim_context.exit_usable) {
		sim_worker_destroy_node(global_sim_context.sim_worker, global_sim_context.current_context);
		global_sim_context.current_context = NULL;
		longjmp(global_sim_context.exit_env,1);
	} else
		dlogf(LOG_ERR, "Module made SNRI exit call when invalid to do so. Ignoring.\n");

	return SNRICALL_SUCCESS;
}

static int snricall_gettime(va_list va) {
	int rv;
	struct timeval * tv;
	va_list vac;

	va_copy(vac, va);
	tv = va_arg(vac, struct timeval*);
	va_end(vac);

	if(tv) {
		tv->tv_sec = global_sim_context.sim_worker->current_time / 1000;
		tv->tv_usec = (global_sim_context.sim_worker->current_time % 1000) * 1000;

		if(sysconfig_get_int("use_wallclock_startup_time_offset")) {
			tv->tv_sec += global_sim_context.sim_worker->wall_time_at_startup.tv_sec;
			ptime_t usec = tv->tv_usec + (global_sim_context.sim_worker->wall_time_at_startup.tv_nsec / 1000);

			/* usecs might roll over the second */
			tv->tv_sec += usec / 1000000;
			tv->tv_usec += usec % 1000000;
		}

		rv = SNRICALL_SUCCESS;
	} else
		rv = SNRICALL_ERROR;

	return rv;
}

static int snricall_log(va_list va) {
	int log_level;
	va_list vac;
	char* format;

	va_copy(vac, va);
	log_level = va_arg(vac, int);
	format = va_arg(vac, char *);
	dlogf_main(log_level, CONTEXT_MODULE, format, vac);
	va_end(vac);

	return SNRICALL_SUCCESS;
}

static int snricall_log_binary(va_list va) {
	int log_level;
	char * data;
	unsigned int data_length;
	va_list vac;
	char* status_prefix;

	va_copy(vac, va);
	log_level = va_arg(vac, int);
	data = va_arg(vac, char *);
	data_length = va_arg(vac, unsigned int);
	va_end(vac);

	status_prefix = dlog_get_status_prefix("module");
	int free_stat = 1;
	if(status_prefix == NULL) {
		status_prefix = "module";
		free_stat = 0;
	}
	int status_len = strlen(status_prefix);
	char logdata[data_length + status_len];
	memcpy(logdata, status_prefix, status_len);
	memcpy(logdata + status_len, data, data_length);

	if(global_sim_context.current_context == NULL)
		dlog_channel_write(0, logdata, data_length + status_len);

	else if(global_sim_context.current_context->log_level >= log_level)
		dlog_channel_write(global_sim_context.current_context->log_channel, logdata, data_length + status_len);

	if(free_stat) {
		free(status_prefix);
	}
	return SNRICALL_SUCCESS;
}

static int snricall_register_globals(va_list va) {
	va_list vac;
	int rv;

	va_copy(vac, va);
	if(global_sim_context.static_context) {
		module_register_globals(global_sim_context.static_context, vac);
		rv = SNRICALL_SUCCESS;
	} else {
		rv = SNRICALL_ERROR;
	}
	va_end(vac);

	return rv;
}

static int snricall_resolve_name(va_list va) {
	/* see snricall_codes.h for explanation of expected params */
	int rv;
	char* name;
	in_addr_t* addr_out;
	va_list vac;

	va_copy(vac, va);
	name = va_arg(vac, char*);
	addr_out = va_arg(vac, in_addr_t*);
	va_end(vac);

	/* do lookup */
	in_addr_t* addr = resolver_resolve_byname(global_sim_context.sim_worker->resolver, name);
	if(addr != NULL) {
		*addr_out = *addr;
		rv = SNRICALL_SUCCESS;
	} else {
		rv = SNRICALL_ERROR;
	}

	return rv;
}

static int snricall_resolve_addr(va_list va) {
	/* see snricall_codes.h for explanation of expected params */
	int rv;
	in_addr_t addr;
	char* name_out;
	int name_out_len;
	va_list vac;

	va_copy(vac, va);
	addr = va_arg(vac, in_addr_t);
	name_out = va_arg(vac, char*);
	name_out_len = va_arg(vac, int);
	va_end(vac);

	/* do lookup */
	char* name = resolver_resolve_byaddr(global_sim_context.sim_worker->resolver, addr);
	if(name != NULL && name_out_len > strlen(name)) {
		strncpy(name_out, name, name_out_len);
		rv = SNRICALL_SUCCESS;
	} else {
		rv = SNRICALL_ERROR;
	}

	return rv;
}

static int snricall_resolve_minbw(va_list va) {
	/* see snricall_codes.h for explanation of expected params */
	in_addr_t addr;
	unsigned int* bw_KBps_out;
	va_list vac;

	va_copy(vac, va);
	addr = va_arg(vac, in_addr_t);
	bw_KBps_out = va_arg(vac, unsigned int*);
	va_end(vac);

	/* do lookup */
	*bw_KBps_out = resolver_get_minbw(global_sim_context.sim_worker->resolver, addr);

	return SNRICALL_SUCCESS;
}

static int snricall_socket_is_readable(va_list va) {
	/* see snricall_codes.h for explanation of expected params */
	int rv;
	int sockd;
	int* bool_out;
	va_list vac;

	va_copy(vac, va);
	sockd = va_arg(vac, int);
	bool_out = va_arg(vac, int*);
	va_end(vac);

	vsocket_tp sock = vsocket_mgr_get_socket(global_sim_context.current_context->vsocket_mgr, sockd);
	if(sock != NULL) {
		*bool_out = vepoll_query_available(sock->vep, VEPOLL_READ);
		rv = SNRICALL_SUCCESS;
	} else {
		rv = SNRICALL_ERROR;
	}

	return rv;
}

static int snricall_socket_is_writable(va_list va) {
	/* see snricall_codes.h for explanation of expected params */
	int rv;
	int sockd;
	int* bool_out;
	va_list vac;

	va_copy(vac, va);
	sockd = va_arg(vac, int);
	bool_out = va_arg(vac, int*);
	va_end(vac);

	vsocket_tp sock = vsocket_mgr_get_socket(global_sim_context.current_context->vsocket_mgr, sockd);
	if(sock != NULL) {
		*bool_out = vepoll_query_available(sock->vep, VEPOLL_WRITE);
		rv = SNRICALL_SUCCESS;
	} else {
		rv = SNRICALL_ERROR;
	}

	return rv;
}

static int snricall_set_loopexit_fn(va_list va) {
	/* see snricall_codes.h for explanation of expected params */
	int rv;
	vevent_mgr_timer_callback_fp fn;
	va_list vac;

	va_copy(vac, va);
	fn = (vevent_mgr_timer_callback_fp) va_arg(vac, void*);
	va_end(vac);

	vsocket_mgr_tp mgr = global_sim_context.current_context->vsocket_mgr;
	if(mgr != NULL && mgr->vev_mgr != NULL) {
		vevent_mgr_set_loopexit_fn(mgr->vev_mgr, fn);
		rv = SNRICALL_SUCCESS;
	} else {
		rv = SNRICALL_ERROR;
	}

	return rv;
}

int snricall(int call_code, ...) {
	va_list va;
	int rv = 0;

	va_start(va, call_code);
	switch(call_code) {
		case SNRICALL_CREATE_TIMER:
			rv = snricall_create_timer(va);
			break;
		case SNRICALL_DESTROY_TIMER:
			rv = snricall_destroy_timer(va);
			break;
		case SNRICALL_EXIT:
			rv = snricall_exit(va); /* will never return */
			break;
		case SNRICALL_GETIP:
			rv = snricall_getip(va);
			break;
		case SNRICALL_GETTIME:
			rv = snricall_gettime(va);
			break;
		case SNRICALL_LOG:
			rv = snricall_log(va);
			break;
		case SNRICALL_LOG_BINARY:
			rv = snricall_log_binary(va);
			break;
		case SNRICALL_REGISTER_GLOBALS:
			rv = snricall_register_globals(va);
			break;
		case SNRICALL_RESOLVE_NAME: {
			rv = snricall_resolve_name(va);
			break;
		}
		case SNRICALL_RESOLVE_ADDR: {
			rv = snricall_resolve_addr(va);
			break;
		}
		case SNRICALL_RESOLVE_BW: {
			rv = snricall_resolve_minbw(va);
			break;
		}
		case SNRICALL_SOCKET_IS_READABLE: {
			rv = snricall_socket_is_readable(va);
			break;
		}
		case SNRICALL_SOCKET_IS_WRITABLE: {
			rv = snricall_socket_is_writable(va);
			break;
		}
		case SNRICALL_SET_LOOPEXIT_FN: {
			rv = snricall_set_loopexit_fn(va);
			break;
		}
	}
	va_end(va);

	return rv;
}

