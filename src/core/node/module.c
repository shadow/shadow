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

#include <glib.h>
#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "global.h"
#include "module.h"
#include "log.h"
#include "snricall.h"

module_mgr_tp module_mgr_create (void) {
	module_mgr_tp mgr = malloc(sizeof(*mgr));
	mgr->modules = g_hash_table_new(g_int_hash, g_int_equal);
	return mgr;
}

static void module_free_cb(gpointer vmod, gint id) {
	module_tp mod = vmod;

	if(mod)
		module_destroy(mod);

	return;
}

void module_mgr_destroy (module_mgr_tp mgr) {
	if(mgr->modules){
		g_hash_table_foreach(mgr->modules, (GHFunc)module_free_cb, NULL);
		g_hash_table_destroy(mgr->modules);
	}
	free(mgr);
	return;
}

module_instance_tp module_create_instance(module_tp module, in_addr_t address) {
	module_instance_tp mod_inst = malloc(sizeof(*mod_inst));

	if(!mod_inst)
		printfault(EXIT_NOMEM, "Out of memory: module_create_instance: instance");

	mod_inst->module = module;

	if(module->globals.total_size > 0) {
		mod_inst->globals = malloc(module->globals.total_size);
		if(!mod_inst->globals)
			printfault(EXIT_NOMEM, "Out of memory: module_create_instance: globals");

		memcpy(mod_inst->globals, module->globals.defaults, module->globals.total_size);
	} else
		mod_inst->globals = NULL;

	return mod_inst;
}

void module_call_destroy(module_instance_tp modinst) {
	if(modinst->module->mod_destroy)
		(*modinst->module->mod_destroy)();
}

void module_destroy_instance(module_instance_tp modinst) {
	if(modinst->globals)
		free(modinst->globals);

	free(modinst);
}

void module_load_globals(module_instance_tp modinst) {
	gint i, didx=0;
	module_tp module = modinst->module;

	/* load globals */
	for(i=0; i<module->globals.num_globals; i++) {
		memcpy(module->globals.g_refs[i], &modinst->globals[didx], module->globals.g_sizes[i]);
		didx += module->globals.g_sizes[i];
	}

	return;
}

void module_save_globals(module_instance_tp modinst) {
	gint i, didx=0;
	module_tp module = modinst->module;

	/*save globals */
	for(i=0; i<module->globals.num_globals; i++) {
		memcpy( &modinst->globals[didx], module->globals.g_refs[i], module->globals.g_sizes[i]);
		didx += module->globals.g_sizes[i];
	}
}

void module_call_instantiate(module_instance_tp modinst, gint argc, gchar* argv[]) {
	(*modinst->module->mod_instantiate)(argc, argv);
}

void module_call_socket_readable(module_instance_tp modinst, gint sockd) {
	(*modinst->module->mod_socket_readable)(sockd);
}

void module_call_socket_writable(module_instance_tp modinst, gint sockd) {
	(*modinst->module->mod_socket_writable)(sockd);
}

module_tp module_get_module(module_mgr_tp mgr, gint module_id) {
	return g_hash_table_lookup(mgr->modules, &module_id);
}

gint module_register_globals( module_tp modinst, va_list va_args ) {
	va_list va;
	guint i, didx=0, total_size=0, num_globals;

	va_copy(va, va_args);
	num_globals = va_arg(va, guint);
	if(num_globals == 0) {
		va_end(va);
		return 0;
	}

	/* init the module space */
	modinst->globals.num_globals = num_globals;
	modinst->globals.g_refs = malloc(sizeof(*modinst->globals.g_refs) * num_globals);
	modinst->globals.g_sizes = malloc(sizeof(*modinst->globals.g_sizes) * num_globals);

	if(!modinst->globals.g_refs || !modinst->globals.g_sizes) {
		va_end(va);
		return 0;
	}

	/* create the global var index */
	for(i=0; i < num_globals; i++) {
		guint argsize = va_arg(va, guint);
		gpointer ref = va_arg(va, gpointer );

		modinst->globals.g_refs[i] = ref;
		modinst->globals.g_sizes[i] = argsize;
		total_size += argsize;
	}
	va_end(va);

	/* allocate deafults memspace */
	modinst->globals.total_size = total_size;
	modinst->globals.defaults = malloc(sizeof(*modinst->globals.defaults) * total_size);
	if(!modinst->globals.defaults)
		return 0;

	/* save default values */
	for(i=0; i<num_globals; i++) {
		memcpy(&modinst->globals.defaults[didx], modinst->globals.g_refs[i], modinst->globals.g_sizes[i]);
		didx += modinst->globals.g_sizes[i];
	}

	return 1;
}

void module_call_init(module_tp modinst) {
	if(modinst)
		(*modinst->mod_init)();
}

void module_call_uninit(module_tp modinst) {
	if(modinst)
		(*modinst->mod_uninit)();
}

void module_destroy(module_tp modinst) {
	gint didx = 0, i;

	if(!modinst)
		return;

	/* load the module defaults ... */
	for(i=0; i<modinst->globals.num_globals; i++) {
		memcpy(modinst->globals.g_refs[i], &modinst->globals.defaults[didx], modinst->globals.g_sizes[i]);
		didx += modinst->globals.g_sizes[i];
	}
	module_call_uninit(modinst);

	if(modinst->globals.defaults)
		free(modinst->globals.defaults);
	if(modinst->globals.g_refs)
		free(modinst->globals.g_refs);
	if(modinst->globals.g_sizes)
		free(modinst->globals.g_sizes);

	dlclose(modinst->handle);
	free(modinst);
}

module_tp module_load(module_mgr_tp mgr, gint id, gchar * module) {
	module_tp mod = NULL;
	gpointer h;

	h = dlopen(module, RTLD_LAZY | RTLD_GLOBAL);

	if(!h) {
		dlogf(LOG_ERR, "Plug-in Subsystem: Unable to load the plug-in: %s\n",  dlerror());
		return NULL;
	}

	mod = malloc(sizeof(*mod));

	if(!mod)
		printfault(EXIT_NOMEM, "module_load: Out of memory");

	mod->handle = h;
	mod->id = id;

	mod->globals.g_refs = NULL;
	mod->globals.g_sizes = NULL;
	mod->globals.defaults = NULL;
	mod->globals.num_globals = 0;


	if(!mod->handle ||
			!(mod->mod_instantiate = 	dlsym(mod->handle, "_plugin_instantiate")) ||
			!(mod->mod_destroy = 		dlsym(mod->handle, "_plugin_destroy")) ||
			!(mod->mod_init = 			dlsym(mod->handle, "_plugin_init")) ||
			!(mod->mod_uninit = 		dlsym(mod->handle, "_plugin_uninit")) ||
			!(mod->mod_socket_readable = dlsym(mod->handle, "_plugin_socket_readable")) ||
			!(mod->mod_socket_writable = dlsym(mod->handle, "_plugin_socket_writable")) ||
			!(mod->mod_snricall_fpmem = dlsym(mod->handle, "_snricall_fpref"))) {

		dlogf(LOG_ERR, "Plug-in Subsystem: Unable to properly acquire all external function in plug-in: %s \n", dlerror());

		if(mod->handle)
			dlclose(mod->handle);

		free(mod);
		return NULL;
	}

	/* make SNRI available */
	*mod->mod_snricall_fpmem = &snricall;

	/* track it */
	g_hash_table_insert(mgr->modules, gint_key(mod->id), mod);

	return mod;
}


