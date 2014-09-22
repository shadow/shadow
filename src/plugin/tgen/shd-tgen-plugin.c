/*
 * See LICENSE for licensing information
 */

#include <string.h>

#include "shd-tgen.h"

/* the state used by this plug-in */
TGenDriver* tgen;
ShadowLogFunc shadowLog;
ShadowCreateCallbackFunc shadowCreateCallback;

/* create a new node using this plug-in */
static void _tgendriver_new(gint argc, gchar* argv[]) {
	/* create the new instance */
	tgen = tgendriver_new(argc, argv, shadowLog, shadowCreateCallback);
}

/* free node state */
static void _tgendriver_free() {
	tgendriver_free(tgen);
}

/* check active sockets for readability/writability */
static void _tgendriver_activate() {
	tgendriver_activate(tgen);
}

/* shadow calls this function for a one-time initialization, and exposes its interface */
void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	/* save shadow's interface functions we will use later */
	shadowLog = shadowlibFuncs->log;
	shadowCreateCallback = shadowlibFuncs->createCallback;

	/* tell shadow which of our functions it can use to call back to our plugin*/
	shadowlibFuncs->registerPlugin(&_tgendriver_new, &_tgendriver_free, &_tgendriver_activate);
}
