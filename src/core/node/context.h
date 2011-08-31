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

#ifndef _context_h
#define _context_h

#include <glib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <setjmp.h>

typedef struct context_provider_t {
	/** module instance data */
	struct module_instance_t * modinst;

	/* virtual buffer and bandwidth manager between vci and sockets */
	struct vsocket_mgr_s * vsocket_mgr;

	/** destination logging channel */
	guchar log_channel;

	/** destination logging channel minimum level */
	gint log_level;
} context_provider_t, *context_provider_tp;

struct context_sys_t {
	jmp_buf exit_env;
	gint exit_usable;

	context_provider_tp current_context;
	context_provider_tp loaded_context;
	struct sim_worker_t * sim_worker;

	/** static context is used for non-node calls to modules; e.g. init and uninit, for purposes
	 * really only related to snri_register_globals
	 */
	struct module_t * static_context;
};

/** global for all objects to access the context */
extern struct context_sys_t global_sim_context;

typedef void (*vci_onrecv_cb_tp)(
			gint, /* socket */
			in_addr_t, /* source address*/
			gint,  /*source port*/
			guint, /* data length */
			gchar * /* data */
		);


typedef void (*dtimer_ontimer_cb_fp)(gint, gpointer );

void context_set_worker(struct sim_worker_t *wo);
void context_execute_init(struct module_t *module);
void context_execute_instantiate(context_provider_tp provider, gint argc, gchar* argv[]);
void context_execute_destroy(context_provider_tp provider);
void context_execute_dtimer_cb(context_provider_tp provider, dtimer_ontimer_cb_fp cb, gint timer_id, gpointer cb_arg);
void context_execute_socket(context_provider_tp provider, guint16 sockd, guint8 can_read, guint8 can_write, guint8 do_read_first);


/* XXX FIXME these should not be visible!!! */
void context_save(void);
void context_load(context_provider_tp provider);

#endif
