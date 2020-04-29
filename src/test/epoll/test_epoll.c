/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "test/test_glib_helpers.h"

/* Tolerance needed for epoll_wait() because the non-shadow
 * version of the test cannot execute instantaneously */
#define TOLERANCE_SECONDS 0.1F

static void _test_epoll_create() {
    int epoll_fd = epoll_create(1);
    assert_true_errno(epoll_fd > 0);

    int close_result = close(epoll_fd);
    assert_true_errno(close_result == 0);
}

static void _test_epoll_create1() {
    int epoll_fd = epoll_create1(0);
    assert_true_errno(epoll_fd > 0);

    int close_result = close(epoll_fd);
    assert_true_errno(close_result == 0);
}

static void _test_epoll_wait_noevents_timeout(int timeout_millis) {
    GTimer* timer = g_timer_new();
    struct epoll_event event = {0};

    int epoll_fd = epoll_create1(0);
    assert_true_errno(epoll_fd > 0);

    /* wait with 0 timeout and no events should return immediately
     * otherwise it should return after timeout milliseconds */
    g_timer_start(timer);
    int epoll_result = epoll_wait(epoll_fd, &event, 1, timeout_millis);
    gdouble elapsed_seconds = g_timer_elapsed(timer, NULL);

    /* epoll_wait() returns the # of ready events, or -1 on error. */
    assert_nonneg_errno(epoll_result);

    int close_result = close(epoll_fd);
    assert_true_errno(close_result == 0);

    /* Cleanup. */
    g_timer_destroy(timer);

    /* Now make sure the correct amount of time passed */
    double timeout_seconds = (double)timeout_millis / 1000.0F;
    if (elapsed_seconds > timeout_seconds + TOLERANCE_SECONDS) {
        g_error("epoll_wait() for %f seconds but %f seconds elapsed",
                timeout_seconds, elapsed_seconds);
        g_test_fail();
    }
}

static void _test_epoll_wait() {
    /* test no timeout (immediate return), and 300 millis. */
    _test_epoll_wait_noevents_timeout(0);
    _test_epoll_wait_noevents_timeout(300);
}

static int _test_fd_write(int fd) {
    return write(fd, "test", 4);
}

static int _test_fd_readCmp(int fd) {
    char buf[5];
    memset(buf, '\0', 5);
    read(fd, buf, 4);
    g_message("read to buf: %s\n", buf);
    return(strncmp(buf, "test", 4) != 0);
}

static void _test_wait_helper(int epoll_fd, struct epoll_event* epoll_ev,
                              int do_oneshot) {
    int total_events_reported = 0;
    int retval = 0;
    int num_iter = do_oneshot ? 5 : 1; // ONESHOT: should report 1 event even if we ask 5 times

    for (int i = 0; i < num_iter; i++) {
        /* Read up to one event with a timeout of 100ms. */
        retval = epoll_wait(epoll_fd, epoll_ev, 1, 100);
        assert_nonneg_errno(retval);
        total_events_reported += retval;
    }

    if (total_events_reported != 1) {
        g_message(
            "error: epoll reported %i events instead of the expected 1 event\n",
            total_events_reported);
        g_test_fail();
    }
}

static void _test_pipe_helper(int do_oneshot) {
    /* Create a set of pipefds
       pfd[0] == read, pfd[1] == write */
    int pfds[2] = {0};
    assert_nonneg_errno(pipe(pfds));

    // ONESHOT: even if there is more than 1 event, we should only get one
    struct epoll_event pevent;
    pevent.events = do_oneshot ? EPOLLIN|EPOLLONESHOT : EPOLLIN;
    pevent.data.fd = pfds[0];

    int efd = epoll_create(1);
    assert_nonneg_errno(epoll_ctl(efd, EPOLL_CTL_ADD, pfds[0], &pevent));

    /* First make sure there's nothing there */
    int ready = epoll_wait(efd, &pevent, 1, 100);
    assert_nonneg_errno(ready);
    assert_true_errstring(ready == 0, "error: pipe empty but marked readable");

    /* Now put information in pipe to be read */
    assert_nonneg_errno(_test_fd_write(pfds[1]));

    /* Check again, should be something to read.
     * in both normal and oneshot cases we should only get back one event */
    _test_wait_helper(efd, &pevent, do_oneshot);

    /* now if we mod, the event should be reported a second time (with or without ONESHOT) */
    pevent.events = do_oneshot ? EPOLLIN|EPOLLONESHOT : EPOLLIN;
    pevent.data.fd = pfds[0];
    assert_nonneg_errno(epoll_ctl(efd, EPOLL_CTL_MOD, pfds[0], &pevent));

    _test_wait_helper(efd, &pevent, do_oneshot);

    /* Make sure we got what expected back */
    assert_true_errno(_test_fd_readCmp(pfds[0]) == 0);

    /* success! */
    close(pfds[0]);
    close(pfds[1]);
    close(efd);
}

static void _test_pipe() { return _test_pipe_helper(0); }

static void _test_pipe_oneshot() { return _test_pipe_helper(1); }

static void _test_pipe_edgetrigger() {
    /* Create a set of pipefds
       pfd[0] == read, pfd[1] == write */
    int pfds[2] = {0};
    assert_nonneg_errno(pipe(pfds));

    struct epoll_event pevent = {
        .events = EPOLLOUT | EPOLLET,
        .data.fd = pfds[1], // writer side
    };

    int efd = epoll_create(1);
    assert_nonneg_errno(epoll_ctl(efd, EPOLL_CTL_ADD, pfds[1], &pevent));

    /* First make sure it is writable */
    int ready = epoll_wait(efd, &pevent, 1, 100);
    assert_nonneg_errno(ready);
    assert_true_errstring(
        ready > 0, "error: pipe empty but not marked writable");

    /* Now put information in pipe to be read */
    assert_nonneg_errno(_test_fd_write(pfds[1]));

    /* now we wrote to pipe. in edge-trigger mode, it should not report that it is writable
     * again since we already collected that event and writable status did not change. */
    ready = epoll_wait(efd, &pevent, 1, 100);
    assert_nonneg_errno(ready);
    assert_true_errstring(
        ready == 0, "error: pipe writable event reported twice in edge-trigger "
                    "mode without changes to descriptor");

    /* but if we run a mod operation, then the writable event should be reported once more */
    pevent.events = EPOLLOUT|EPOLLET;
    pevent.data.fd = pfds[1];
    assert_nonneg_errno(epoll_ctl(efd, EPOLL_CTL_MOD, pfds[1], &pevent));

    ready = epoll_wait(efd, &pevent, 1, 100);
    assert_nonneg_errno(ready);
    assert_true_errstring(
        ready > 0, "error: pipe writable event was not reported in "
                   "edge-trigger mode after epoll_ctl MOD operation\n");

    /* success! */
    close(pfds[0]);
    close(pfds[1]);
    close(efd);
}

// TODO re-enable (and expand) testing of epoll on files once proper support
// is added to Shadow
// static int _test_creat() {
//    int fd = creat("testepoll.txt", 0);
//    if(fd < 0) {
//        fprintf(stdout, "error: could not create testepoll.txt\n");
//        return EXIT_FAILURE;
//    }
//
//    /* poll will check when testpoll has info to read */
//    struct epoll_event pevent;
//    pevent.events = EPOLLIN;
//    pevent.data.fd = fd;
//
//    int efd = epoll_create(1);
//    if(epoll_ctl(efd, EPOLL_CTL_ADD, pevent.data.fd, &pevent) == 0) {
//        fprintf(stdout, "error: epoll_ctl should have failed\n");
//        unlink("testepoll.txt");
//        return EXIT_FAILURE;
//    }
//
//    if(errno != EPERM) {
//        fprintf(stdout, "error: errno is '%i' instead of 1 (EPERM)\n",errno);
//        unlink("testepoll.txt");
//        return EXIT_FAILURE;
//    }
//
//    unlink("testepoll.txt");
//    return EXIT_SUCCESS;
//}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/epoll/epoll_create", _test_epoll_create);
    g_test_add_func("/epoll/epoll_create1", _test_epoll_create1);
    g_test_add_func("/epoll/epoll_wait", _test_epoll_wait);
    g_test_add_func("/epoll/epoll_pipe", _test_pipe);
    g_test_add_func("/epoll/epoll_pipe_oneshot", _test_pipe_oneshot);
    g_test_add_func("/epoll/epoll_pipe_edgetrigger", _test_pipe_edgetrigger);
    // TODO: expand testing epoll on files, sockets, timerfd?
    // Note that the timerfd test already uses epoll extensively.
    g_test_run();

    return 0;
}
