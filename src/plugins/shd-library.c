/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

#include <glib.h>
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

int shadowlib_register(PluginNewInstanceFunc new, PluginNotifyFunc free, PluginNotifyFunc notify) {
	Plugin* currentPlugin = worker_getCurrentPlugin();
	plugin_setShadowContext(currentPlugin, TRUE);

	plugin_registerResidentState(currentPlugin, new, free, notify);

	plugin_setShadowContext(currentPlugin, FALSE);
	return TRUE;
}

void shadowlib_log(ShadowLogLevel level, const char* functionName, const char* format, ...) {
	Plugin* currentPlugin = worker_getCurrentPlugin();
	plugin_setShadowContext(currentPlugin, TRUE);

	GLogLevelFlags glevel = 0;
	switch(level) {
	case SHADOW_LOG_LEVEL_ERROR:
		glevel = G_LOG_LEVEL_ERROR;
		break;
	case SHADOW_LOG_LEVEL_CRITICAL:
		glevel = G_LOG_LEVEL_CRITICAL;
		break;
	case SHADOW_LOG_LEVEL_WARNING:
		glevel = G_LOG_LEVEL_WARNING;
		break;
	case SHADOW_LOG_LEVEL_MESSAGE:
		glevel = G_LOG_LEVEL_MESSAGE;
		break;
	case SHADOW_LOG_LEVEL_INFO:
		glevel = G_LOG_LEVEL_INFO;
		break;
	case SHADOW_LOG_LEVEL_DEBUG:
		glevel = G_LOG_LEVEL_DEBUG;
		break;
	default:
		glevel = G_LOG_LEVEL_MESSAGE;
		break;
	}

	va_list variableArguments;
	va_start(variableArguments, format);

	const gchar* domain = g_quark_to_string(*plugin_getID(currentPlugin));
	logging_logv(domain, glevel, functionName, format, variableArguments);

	va_end(variableArguments);

	plugin_setShadowContext(currentPlugin, FALSE);
}

static void _shadowlib_executeCallbackInPluginContext(gpointer data, gpointer argument) {
	ShadowPluginCallbackFunc callback = argument;
	callback(data);
}

void shadowlib_createCallback(ShadowPluginCallbackFunc callback, void* data, uint millisecondsDelay) {
	Plugin* currentPlugin = worker_getCurrentPlugin();
	plugin_setShadowContext(currentPlugin, TRUE);

	application_callback(worker_getCurrentApplication(),
			_shadowlib_executeCallbackInPluginContext, data, callback, millisecondsDelay);

	plugin_setShadowContext(currentPlugin, FALSE);
}

int shadowlib_getBandwidth(in_addr_t ip, uint* bwdown, uint* bwup) {
	if(!bwdown && !bwup) {
		return TRUE;
	}

	gboolean success = FALSE;

	Plugin* currentPlugin = worker_getCurrentPlugin();
	plugin_setShadowContext(currentPlugin, TRUE);

	Address* hostAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32)ip);
	if(hostAddress) {
		GQuark id = (GQuark) address_getID(hostAddress);
		if(bwdown) {
			*bwdown = worker_getNodeBandwidthDown(id, ip);
		}
		if(bwup) {
			*bwup = worker_getNodeBandwidthUp(id, ip);
		}
		success = TRUE;
	}

	plugin_setShadowContext(currentPlugin, FALSE);

	return success;
}

extern const void* intercept_RAND_get_rand_method(void);
int shadowlib_cryptoSetup(int numLocks, void** shadowLockFunc, void** shadowIdFunc, const void** shadowRandomMethod) {
	g_assert(shadowLockFunc && shadowIdFunc && shadowRandomMethod);

	*shadowLockFunc = &system_cryptoLockingFunc;
	*shadowIdFunc = &system_cryptoIdFunc;
	*shadowRandomMethod = intercept_RAND_get_rand_method();

	return worker_cryptoSetup(numLocks);
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
