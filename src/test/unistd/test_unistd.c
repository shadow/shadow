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

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/unistd/getpid_nodeps", _test_getpid_nodeps);
    // TODO: Support kill in shadow (and/or find another way of validating the
    // pid)
    if (getenv("SHADOW_SPAWNED") == NULL) {
        g_test_add_func("/unistd/getpid_kill", _test_getpid_kill);
    }
    g_test_run();

    return 0;
}
