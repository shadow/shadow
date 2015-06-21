/*
 * See LICENSE for licensing information
 */

#include <string.h>

#include "shd-test.h"

/* the state used by this plug-in */
typedef struct _TestData {
    Test* test;
    ShadowLogFunc logf;
    ShadowCreateCallbackFunc callf;
} TestData;

static TestData testTempGlobalData;

/* create a new node using this plug-in */
static void _testplugin_new(gint argc, gchar* argv[]) {
    /* create the new instance */
    testTempGlobalData.test = test_new(argc, argv, testTempGlobalData.logf, testTempGlobalData.callf);
}

/* free node state */
static void _testplugin_free() {
    if(testTempGlobalData.test) {
        test_free(testTempGlobalData.test);
    }
}

/* check active sockets for readability/writability */
static void _testplugin_activate() {
    if(testTempGlobalData.test) {
        test_activate(testTempGlobalData.test);
    }
}

/* shadow calls this function for a one-time initialization, and exposes its interface */
void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
    /* save shadow's interface functions we will use later */
    testTempGlobalData.logf = shadowlibFuncs->log;
    testTempGlobalData.callf = shadowlibFuncs->createCallback;
    testTempGlobalData.test = NULL;

    /* tell shadow which of our functions it can use to call back to our plugin*/
    shadowlibFuncs->registerPlugin(&_testplugin_new, &_testplugin_free, &_testplugin_activate);
}
