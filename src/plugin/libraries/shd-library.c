/*
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
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	debug("shadowlib_register called");

	/* collect the variable length argument list*/
	va_list variableArguments;
	va_start(variableArguments, nVariables);

	plugin_registerResidentState(worker->cached_plugin, callbackFunctions, nVariables, variableArguments);

	va_end(variableArguments);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
	return TRUE;
}

void shadowlib_log(GLogLevelFlags level, const gchar* functionName, gchar* format, ...) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	va_list variableArguments;
	va_start(variableArguments, format);

	const gchar* domain = g_quark_to_string(worker->cached_plugin->id);
	logging_logv(domain, level, functionName, format, variableArguments);

	va_end(variableArguments);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
}

in_addr_t shadowlib_resolveHostname(gchar* name) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	in_addr_t ip = (in_addr_t) internetwork_resolveName(worker->cached_engine->internet, name);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
	return ip;
}

gboolean shadowlib_resolveIPAddress(in_addr_t addr, gchar* name_out, gint name_out_len) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	const gchar* name = internetwork_resolveID(worker->cached_engine->internet, (GQuark)addr);
	gboolean isSuccess = FALSE;
	if(name != NULL && name_out_len > strlen(name)) {
		strncpy(name_out, name, name_out_len);
		isSuccess = TRUE;
	}

	plugin_setShadowContext(worker->cached_plugin, FALSE);
	return isSuccess;
}

in_addr_t shadowlib_getIPAddress() {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	in_addr_t ip = (in_addr_t) worker->cached_node->id;

	plugin_setShadowContext(worker->cached_plugin, FALSE);
	return ip;
}

gboolean shadowlib_getHostname(gchar* name_out, gint name_out_len) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	in_addr_t ip = (in_addr_t) worker->cached_node->id;
	gboolean hostname = shadowlib_resolveIPAddress(ip, name_out, name_out_len);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
	return hostname;
}

void _shadowlib_executeCallbackInPluginContext(gpointer data, gpointer argument) {
	ShadowPluginCallbackFunc callback = argument;
	callback(data);
}

void _shadowlib_timerExpired(gpointer data, gpointer argument) {
	Worker* worker = worker_getPrivate();
	Application* a = worker->cached_node->application;
	Plugin* plugin = worker_getPlugin(a->software);
	plugin_executeGeneric(plugin, a->state, _shadowlib_executeCallbackInPluginContext, data, argument);
}

void shadowlib_createCallback(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	/* the plugin wants a callback. since we need it to happen in the plug-in
	 * context, we create a callback to our own function, then execute theirs
	 */
	CallbackEvent* event = callback_new(_shadowlib_timerExpired, data, callback);
	SimulationTime nanos = SIMTIME_ONE_MILLISECOND * millisecondsDelay;
	worker_scheduleEvent((Event*)event, nanos, worker->cached_node->id);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
}

void shadowlib_setLoopExit(ShadowPluginCallbackFunc callback) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	vevent_mgr_set_loopexit_fn(worker->cached_node->vsocket_mgr->vev_mgr,
			(vevent_mgr_timer_callback_fp)callback);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
}

guint32 shadowlib_getBandwidthFloor(in_addr_t ip) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	Node* n = internetwork_getNode(worker->cached_engine->internet, (GQuark)ip);
	guint32 down = n->vsocket_mgr->vt_mgr->KBps_down;
	guint32 up = n->vsocket_mgr->vt_mgr->KBps_up;

	plugin_setShadowContext(worker->cached_plugin, FALSE);
	return down < up ? down : up;
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
	&shadowlib_createCallback,
	&shadowlib_setLoopExit,
	&shadowlib_getBandwidthFloor,
};
