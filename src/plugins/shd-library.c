/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

#include <stdarg.h>
#include <string.h>

typedef struct _CallbackData CallbackData;
struct _CallbackData {
	gpointer applicationData;
	Application* application;
};

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

void shadowlib_createCallback(ShadowPluginCallbackFunc callback, gpointer data, guint millisecondsDelay) {
	Worker* worker = worker_getPrivate();
	plugin_setShadowContext(worker->cached_plugin, TRUE);

	application_callback(worker->cached_application,
			_shadowlib_executeCallbackInPluginContext, data, callback, millisecondsDelay);

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
