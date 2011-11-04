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

#include <netinet/in.h>

#include "global.h"
#include "context.h"
#include "sim.h"

struct context_sys_t global_sim_context;
//static void context_save(void);
//static void context_load(context_provider_tp provider);

#if 0
static void context_swap(context_provider_tp provider);
static void context_swap(context_provider_tp provider) {
	/* save current context */
	context_save();
	/* load new context */
	context_load(provider);
}
#endif

void context_load(context_provider_tp provider) {
	if(provider != global_sim_context.current_context) {
		/* we will now be executing in the context of a module */
		global_sim_context.current_context = provider;
	}
	if(provider && provider != global_sim_context.loaded_context) {
		/* our globals are not loaded, so load them */
		module_load_globals(provider->modinst);
		global_sim_context.loaded_context = provider;
	}
}

void context_save(void) {
	/* current_context is NULL if we already saved, so we wont save twice without loading in between */
	if(global_sim_context.current_context) {
		module_save_globals(global_sim_context.current_context->modinst);
		global_sim_context.current_context = NULL;
	}
}

void context_set_worker(sim_worker_tp wo) {
	global_sim_context.sim_worker = wo;
}

void context_execute_init(module_tp module) {
	global_sim_context.static_context = module;
	module_call_init(module);
	global_sim_context.static_context = NULL;
}

void context_execute_instantiate(context_provider_tp provider, int argc, char* argv[]) {
	if(!provider)
		return;

	/* swap out env for this provider */
	context_load(provider);
	global_sim_context.exit_usable = 1;

	if(setjmp(global_sim_context.exit_env) == 1)  /* module has been destroyed if we get here. (sim_context.current_context will be NULL) */
		return;

	else
		module_call_instantiate(provider->modinst, argc, argv);

	/* swap back to dvn holding */
	context_save();

	return;
}

void context_execute_destroy(context_provider_tp provider) {
	/* swap out env for this provider */
	context_load(provider);
	global_sim_context.exit_usable = 0;

	/* send out destroy event */
	module_call_destroy(provider->modinst);

	/* we don't swap back here ... module is "destroyed" */

	return;
}

void context_execute_socket(context_provider_tp provider, int sockd, uint8_t can_read, uint8_t can_write, uint8_t do_read_first) {
	if(!provider){
		return;
	}
//		context_provider_tp current_provider = global_sim_context.current_context;
//
//		/* swap out current provider, if its different  */
//		if(current_provider != provider) {
//			/* save current variables */
//			context_save();
//
//			/* swap in new provider */
//			context_load(provider);
//			global_sim_context.exit_usable = 1;
//		}
	context_load(provider);
	global_sim_context.exit_usable = 1;
	if(setjmp(global_sim_context.exit_env) == 1) {
		/* module has been destroyed (sim_context.current_context will be NULL) */
		return;
	} else {
		/* module exists */
		/* TODO refactor this! */
		if(do_read_first) {
			if(can_read) {
				module_call_socket_readable(provider->modinst, (int)sockd);
			}
			if(can_write) {
				module_call_socket_writable(provider->modinst, (int)sockd);
			}
		} else {
			if(can_write) {
				module_call_socket_writable(provider->modinst, (int)sockd);
			}
			if(can_read) {
				module_call_socket_readable(provider->modinst, (int)sockd);
			}
		}
	}
	context_save();
//		if(current_provider != provider) {
//			/* swap back to our saved provider */
//			context_save();
//			context_load(current_provider);
//		}
}

void context_execute_dtimer_cb(context_provider_tp provider, dtimer_ontimer_cb_fp cb, int timer_id, void * cb_arg) {
	if(!provider)
		return;

	context_load(provider);
	global_sim_context.exit_usable = 1;

	if(setjmp(global_sim_context.exit_env) == 1) /* module has been destroyed if we get here. (sim_context.current_context will be NULL) */
			return;

	else
		(*cb)(timer_id, cb_arg);

	/* swap back to dvn holding */
	context_save();

	return;
}
