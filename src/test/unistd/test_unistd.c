#include <glib.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>

#include "test/test_glib_helpers.h"

// Tests that the results are plausible, but can't really validate that it's our
// pid without depending on other functionality.
static void _test_getpid_nodeps() {
    int pid = getpid();
    g_assert_cmpint(pid,>,0);
    g_assert_cmpint(getpid(),==,pid);
}

static int sigaction_inc_count = 0;
static void sigaction_inc(int sig) {
    ++sigaction_inc_count;
}

// Validates that the returned pid is ours by using it to send a signal to
// ourselves.
static void _test_getpid_kill() {
    struct sigaction action = {.sa_handler = sigaction_inc};
    assert_nonneg_errno(sigaction(SIGUSR1, &action, NULL));

    int pid = getpid();
    sigaction_inc_count = 0;
    assert_nonneg_errno(kill(pid, SIGUSR1));
    g_assert_cmpint(sigaction_inc_count, ==, 1);
}

static void _test_gethostname(gconstpointer gp_nodename) {
    const char* nodename = gp_nodename;
    char buf[1000] = {0};

    // Invalid pointer. Documented to return errno=EFAULT in gethostname(2),
    // but segfaults on Ubuntu 18.
    //
    // g_assert_cmpint(gethostname(NULL+1, 100),==,-1);
    // assert_errno_is(EFAULT);

    // Negative len. Documented to return errno=EINVAL in gethostname(2), but
    // segfaults on Ubuntu 18.
    //
    // g_assert_cmpint(gethostname(buf, -1),==,-1);
    // assert_errno_is(EINVAL);

    // Short buffer
    g_assert_cmpint(gethostname(buf, 1),==,-1);
    assert_errno_is(ENAMETOOLONG);

    // Get the hostname and compare with expected name passed through the
    // command-line.
    assert_nonneg_errno(gethostname(buf, sizeof(buf)));
    g_assert_cmpstr(buf,==,nodename);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s expected-node-name\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char* nodename = argv[1];

    g_test_add_func("/unistd/getpid_nodeps", _test_getpid_nodeps);
    // TODO: Support `kill` in shadow (and/or find another way of validating
    // the pid)
    if (getenv("SHADOW_SPAWNED") == NULL) {
        g_test_add_func("/unistd/getpid_kill", _test_getpid_kill);
    }

    g_test_add_data_func("/unistd/gethostname", nodename, _test_gethostname);

    g_test_run();

    return 0;
}
