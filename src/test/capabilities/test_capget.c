#include <glib.h>
#include <sys/capability.h>

#include "test/test_glib_helpers.h"

static void test_capget() {
    struct __user_cap_header_struct hdr;
    hdr.version = _LINUX_CAPABILITY_VERSION_3;
    hdr.pid = 0;

    // Make some non-empty capabilities
    struct __user_cap_data_struct nonempty = {
        .effective = 1,
        .permitted = 1,
        .inheritable = 1,
    };
    // Put the non-empty to the array so that we check that it will be
    // written to zeroes later
    struct __user_cap_data_struct data[2] = {nonempty, nonempty};

    assert_nonneg_errno(capget(&hdr, data));

    for(int i = 0; i < 2; i++) {
        g_assert_cmpint(data[i].effective, ==, 0);
        g_assert_cmpint(data[i].permitted, ==, 0);
        g_assert_cmpint(data[i].inheritable, ==, 0);
    }
}

static void test_capget_null_datap() {
    struct __user_cap_header_struct hdr;
    hdr.version = _LINUX_CAPABILITY_VERSION_3;
    hdr.pid = 0;

    assert_nonneg_errno(capget(&hdr, NULL));
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/capabilities/capget", test_capget);
    g_test_add_func("/capabilities/capget_null_datap", test_capget_null_datap);
    return g_test_run();
}
