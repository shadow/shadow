#include "shd-browser.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
Browser browserstate;

/* function table for Shadow so it knows how to call us */
PluginFunctionTable browser_pluginFunctions = {
	&browserplugin_new, &browserplugin_free, &browserplugin_ready,
};

void __shadow_plugin_init__(ShadowlibFunctionTable* shadowlibFuncs) {
	g_assert(shadowlibFuncs);
  shadowlibFuncs->log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "browser-plugin init: nothing implemented yet!");
  
	/* start out with cleared state */
	memset(&browserstate, 0, sizeof(Browser));

	/* save the functions Shadow makes available to us */
	browserstate.shadowlibFuncs = *shadowlibFuncs;

	/*
	 * tell shadow which of our functions it can use to notify our plugin,
	 * and allow it to track our state for each instance of this plugin
	 *
	 * we 'register' our function table, and 1 variable.
	 */
	gboolean success = browserstate.shadowlibFuncs.registerPlugin(&browser_pluginFunctions, 1, sizeof(Browser), &browserstate);

	/* we log through Shadow by using the log function it supplied to us */
	if(success) {
		browserstate.shadowlibFuncs.log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, "successfully registered browser plug-in state");
	} else {
		browserstate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "error registering browser plug-in state");
	}
}

void browserplugin_new(gint argc, gchar* argv[]) {
  browserstate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "browserplugin_new called");
}

void browserplugin_free() {
  browserstate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "browserplugin_free called");
}

void browserplugin_ready() {
  browserstate.shadowlibFuncs.log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, "browserplugin_ready called");
}