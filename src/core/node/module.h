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

#ifndef _modloader_h
#define _modloader_h

#include <netinet/in.h>
#include <stdarg.h>
#include <glib-2.0/glib.h>

#include "snricall.h"
#include "hashtable.h"

typedef void (*module_modfunc_init_fp)();
typedef void (*module_modfunc_uninit_fp)();
typedef void (*module_modfunc_instantiate_fp)(int, char *[]);
typedef void (*module_modfunc_destroy_fp)();
typedef void (*module_modfunc_socket_readable_fp)(int);
typedef void (*module_modfunc_socket_writable_fp)(int);

typedef struct module_globals_t {
	void ** g_refs;
	unsigned int * g_sizes;
	unsigned char * defaults;
	unsigned int total_size;
	unsigned int num_globals;
} module_globals_t, *module_globals_tp;

typedef struct module_t {
	int id;
	void * handle;

	module_globals_t globals;

	module_modfunc_init_fp mod_init;
	module_modfunc_instantiate_fp mod_instantiate;
	module_modfunc_destroy_fp mod_destroy;
	module_modfunc_uninit_fp mod_uninit;
	module_modfunc_socket_readable_fp mod_socket_readable;
	module_modfunc_socket_writable_fp mod_socket_writable;

	snricall_fp * mod_snricall_fpmem;

	struct module_mgr_t * mgr;
} module_t, *module_tp;

typedef struct module_mgr_t {
	GHashTable *modules;
} * module_mgr_tp;

typedef struct module_instance_t {
	char * globals;
	module_tp module;
} module_instance_t, *module_instance_tp;


/* Creates a module manager with the given callbacks */
module_mgr_tp module_mgr_create (void);

void module_call_instantiate(module_instance_tp modinst, int argc, char* argv[]);
void module_call_init(module_tp modinst);
void module_call_uninit(module_tp modinst);
void module_call_destroy(module_instance_tp modinst);
void module_call_socket_readable(module_instance_tp modinst, int sockd);
void module_call_socket_writable(module_instance_tp modinst, int sockd);

/* registers a set of globals for usage in a particular DVN module. saves
 * the defaults */
int module_register_globals( module_tp mod, va_list va_args );

/* Destroys a module manager */
void module_mgr_destroy (module_mgr_tp);

/* Loads a module to the given manager */
module_tp module_load(module_mgr_tp mgr, int id, char * module);

void module_destroy(module_tp mod);

module_instance_tp module_create_instance(module_tp module, in_addr_t address);

void module_destroy_instance(module_instance_tp modinst);

void module_load_globals(module_instance_tp modinst);

void module_save_globals(module_instance_tp modinst);

module_tp module_get_module(module_mgr_tp mgr, int module_id);


#endif
