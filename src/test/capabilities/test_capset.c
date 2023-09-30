#include <glib.h>
#include <stdint.h>
#include <sys/capability.h>

#include "test/test_glib_helpers.h"

static void test_capset() {
    struct __user_cap_header_struct hdr;
    hdr.version = _LINUX_CAPABILITY_VERSION_3;
    hdr.pid = 0;

    struct __user_cap_data_struct empty = {
        .effective = 0,
        .permitted = 0,
        .inheritable = 0,
    };
    struct __user_cap_data_struct data[2] = {empty, empty};

    assert_nonneg_errno(capset(&hdr, data));
}

static void test_capset_nonempty() {
    struct __user_cap_header_struct hdr;
    hdr.version = _LINUX_CAPABILITY_VERSION_3;
    hdr.pid = 0;

    struct __user_cap_data_struct full = {
        .effective = UINT32_MAX,
        .permitted = UINT32_MAX,
        .inheritable = UINT32_MAX,
    };
    struct __user_cap_data_struct data[2] = {full, full};

    // We don't allow the plugin to set any capability
    assert_true_errno(capset(&hdr, data) == -1);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/capabilities/capset", test_capset);
    g_test_add_func("/capabilities/capset_nonempty", test_capset_nonempty);
    return g_test_run();
}
