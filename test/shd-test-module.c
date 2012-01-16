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

/*
 * ./run_plugin_test.sh
 *
 * -- or --
 *
 * (see commands in shd-test-plugin first)
 * gcc `pkg-config --cflags --libs glib-2.0 gmodule-2.0` shd-test-module.c -o shd-test-module
 * ./shd-test-module
 */

#include <glib.h>
#include <gmodule.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#define PLUGININITSYMBOL "__init__"
typedef void (*InitFunc)(void);

void load(gchar* path, gboolean useGlib) {
	GModule* handle;

	if(useGlib) {
		handle = g_module_open(path, G_MODULE_BIND_LAZY|G_MODULE_BIND_LOCAL);
	} else {
		// note: RTLD_DEEPBIND -> prefer local symbols, but still have global access
		handle = dlopen(path, RTLD_LAZY|RTLD_LOCAL);
	}

	if(handle) {
		g_message("successfully loaded private plug-in '%s'", path);
	} else {
		g_error("unable to load private plug-in '%s'", path);
	}

	/* make sure it has the required init function */
	InitFunc func;
	gboolean success;

	if(useGlib) {
		success = g_module_symbol(handle, PLUGININITSYMBOL, (gpointer)&func);
	} else {
		func = dlsym(handle, PLUGININITSYMBOL);
		success = func ? TRUE : FALSE;
	}

	if(success) {
		g_message("succesfully found function '%s' in plugin '%s'", PLUGININITSYMBOL, path);
	} else {
		g_error("unable to find the required function symbol '%s' in plug-in '%s'",
				PLUGININITSYMBOL, path);
	}

	func();

//	Need to keep the modules open for the test
//	if(useGlib) {
//		g_module_close(handle);
//	} else {
//		dlclose(handle);
//	}
}

/*
 * Without G_MODULE_BIND_LOCAL, the result is:
	1 after increment
	2 after increment
	3 after increment
	4 after increment

 * else the result is:
	1 after increment
	1 after increment
	1 after increment
	1 after increment

 * So, we need G_MODULE_BIND_LOCAL to keep variables private to the plugin.
 */

int main(void) {
  g_thread_init(NULL);
	load("/tmp/testplugin1.so", TRUE);
	load("/tmp/testplugin2.so", TRUE);
	load("/tmp/testplugin3.so", FALSE);
	load("/tmp/testplugin4.so", FALSE);
	return EXIT_SUCCESS;
}
