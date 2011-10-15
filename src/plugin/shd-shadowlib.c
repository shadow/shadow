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

gboolean shadowlib_register(PluginFunctionTable* callbackFunctions, guint nVariables, ...) {
	debug("shadowlib_register called");

	/* collect the variable length argument list*/
	va_list variableArguments;
	va_start(variableArguments, nVariables);

	Worker* worker = worker_getPrivate();
	plugin_registerResidentState(worker->cached_plugin, callbackFunctions, nVariables, variableArguments);

	va_end(variableArguments);

	return TRUE;
}

void shadowlib_log(gchar* format, ...) {
	va_list variableArguments;
	va_start(variableArguments, format);

	/* the NULL is the function name. we dont get that from the plug-in, and
	 * it might not even make sense anyway.
	 */
	logging_logv(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, NULL, format, variableArguments);

	va_end(variableArguments);
}

in_addr_t shadowlib_resolveHostname(gchar* name) {
	Worker* worker = worker_getPrivate();
	return (in_addr_t) internetwork_resolveName(worker->cached_engine->internet, name);
}

gboolean shadowlib_resolveIPAddress(in_addr_t addr, gchar* name_out, gint name_out_len) {
	Worker* worker = worker_getPrivate();
	const gchar* name = internetwork_resolveID(worker->cached_engine->internet, (GQuark)addr);
	if(name != NULL && name_out_len > strlen(name)) {
		strncpy(name_out, name, name_out_len);
		return TRUE;
	} else {
		return FALSE;
	}
}

in_addr_t shadowlib_getIPAddress() {
	Worker* worker = worker_getPrivate();
	return (in_addr_t) worker->cached_node->id;
}

gboolean shadowlib_getHostname(gchar* name_out, gint name_out_len) {
	in_addr_t ip = shadowlib_getIPAddress();
	return shadowlib_resolveIPAddress(ip, name_out, name_out_len);
}

/* we send this FunctionTable to each plug-in so it has pointers to our functions.
 * we use this to export shadow functionality to plug-ins. */
ShadowlibFunctionTable shadowlibFunctionTable = {
	&shadowlib_register,
	&shadowlib_log,
	&shadowlib_resolveHostname,
	&shadowlib_resolveIPAddress,
	&shadowlib_getHostname,
	&shadowlib_getIPAddress,
};
