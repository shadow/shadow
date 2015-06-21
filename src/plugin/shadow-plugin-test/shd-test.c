/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shd-test.h"

struct _Test {
    ShadowLogFunc logf;
    ShadowCreateCallbackFunc callf;
    guint magic;
};

Test* test_new(gint argc, gchar* argv[], ShadowLogFunc logf, ShadowCreateCallbackFunc callf) {
    Test* test = g_new0(Test, 1);

    test->magic = TEST_MAGIC;
    test->logf = logf;
    test->callf = callf;

    return test;
}

void test_free(Test* test) {
    TEST_ASSERT(test);
    test->magic = 0;
    g_free(test);
}

void test_activate(Test* test) {
    TEST_ASSERT(test);
}
