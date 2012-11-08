/*
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

#include "shadow.h"

#include <stdarg.h>
#include <string.h>

/**
 * This file provides functionality exported to plug-ins. It mostly provides
 * a common interface and re-directs to the appropriate shadow function.
 */

gboolean shadowlib_register(PluginNewInstanceFunc new, PluginNotifyFunc free, PluginNotifyFunc notify) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	plugin_registerResidentState(worker->cached_plugin, new, free, notify);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
	return TRUE;
}

void shadowlib_log(GLogLevelFlags level, const gchar* functionName, gchar* format, ...) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	va_list variableArguments;
	va_start(variableArguments, format);

	const gchar* domain = g_quark_to_string(*plugin_getID(worker->cached_plugin));
	logging_logv(domain, level, functionName, format, variableArguments);

	va_end(variableArguments);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
}

static void _shadowlib_executeCallbackInPluginContext(gpointer data, gpointer argument) {
	ShadowPluginCallbackFunc callback = argument;
	callback(data);
}

static void _shadowlib_timerExpired(gpointer data, gpointer argument) {
	Worker* worker = worker_getPrivate();
	Application* a = node_getApplication(worker->cached_node);
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

	/* callback to our own node */
	worker_scheduleEvent((Event*)event, nanos, 0);

	plugin_setShadowContext(worker->cached_plugin, FALSE);
}

gboolean shadowlib_getBandwidth(in_addr_t ip, guint* bwdown, guint* bwup) {
	if(!bwdown || !bwup) {
		return FALSE;
	}

	gboolean success = FALSE;

	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	Node* n = internetwork_getNode(worker_getInternet(), (GQuark)ip);
	if(n) {
		NetworkInterface* interface = node_lookupInterface(n, ip);
		if(interface) {
			*bwdown = (guint)networkinterface_getSpeedDownKiBps(interface);
			*bwup = (guint)networkinterface_getSpeedUpKiBps(interface);
			success = TRUE;
		}
	}

	plugin_setShadowContext(worker->cached_plugin, FALSE);

	return success;
}

extern const void* intercept_RAND_get_rand_method(void);
gboolean shadowlib_cryptoSetup(gint numLocks, gpointer* shadowLockFunc, gpointer* shadowIdFunc, gconstpointer* shadowRandomMethod) {
	g_assert(shadowLockFunc && shadowIdFunc && shadowRandomMethod);
	Worker* worker = worker_getPrivate();

	*shadowLockFunc = &system_cryptoLockingFunc;
	*shadowIdFunc = &system_cryptoIdFunc;
	*shadowRandomMethod = intercept_RAND_get_rand_method();

	return engine_cryptoSetup(worker->cached_engine, numLocks);
}

/* we send this FunctionTable to each plug-in so it has pointers to our functions.
 * we use this to export shadow functionality to plug-ins. */
ShadowFunctionTable shadowlibFunctionTable = {
	&shadowlib_register,
	&shadowlib_log,
	&shadowlib_createCallback,
	&shadowlib_getBandwidth,
	&shadowlib_cryptoSetup,
};
