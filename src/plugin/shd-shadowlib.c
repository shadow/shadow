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

#include "shadow.h"

#include <stdarg.h>
#include <string.h>

/**
 * This file provides functionality exported to plug-ins. It mostly provides
 * a common interface and re-directs to the appropriate shadow function.
 */

gboolean shadowlib_register(PluginVTable* callbackFunctions, guint nVariables, ...) {
	debug("shadowlib_register called");

	/* collect the variable length argument list*/
	va_list variableArguments;
	va_start(variableArguments, nVariables);

	/* these are the physical memory addresses and sizes for each variable */
	PluginState* residentState = pluginstate_new(callbackFunctions, nVariables, variableArguments);

	Worker* worker = worker_getPrivate();
	plugin_registerResidentState(worker->cached_plugin, residentState);

	va_end(variableArguments);

	return TRUE;
}

void shadowlib_log(gchar* format, ...) {
	va_list variableArguments;
	va_start(variableArguments, format);

	logging_logv(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, format, variableArguments);

	va_end(variableArguments);
}

in_addr_t shadowlib_resolveHostname(gchar* name) {
	Worker* worker = worker_getPrivate();
	in_addr_t* ipAddress = resolver_resolve_byname(worker->cached_engine->resolver, name);
	return *ipAddress;
}

gboolean shadowlib_resolveIPAddress(in_addr_t addr, gchar* name_out, gint name_out_len) {
	Worker* worker = worker_getPrivate();
	gchar* name = resolver_resolve_byaddr(worker->cached_engine->resolver, addr);
	if(name != NULL && name_out_len > strlen(name)) {
		strncpy(name_out, name, name_out_len);
		return TRUE;
	} else {
		return FALSE;
	}
}

in_addr_t shadowlib_getIPAddress() {
	Worker* worker = worker_getPrivate();
	return worker->cached_node->id;
}

gboolean shadowlib_getHostname(gchar* name_out, gint name_out_len) {
	in_addr_t ip = shadowlib_getIPAddress();
	return shadowlib_resolveIPAddress(ip, name_out, name_out_len);
}

/* we send this VTable to each plug-in so it has pointers to our functions.
 * we use this to export shadow functionality to plug-ins. */
ShadowlibVTable shadowlibVTable = {
	&shadowlib_register,
	&shadowlib_log,
	&shadowlib_resolveHostname,
	&shadowlib_resolveIPAddress,
	&shadowlib_getHostname,
	&shadowlib_getIPAddress,
};
