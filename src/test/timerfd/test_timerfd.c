#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "test/test_common.h"
#include "test/test_glib_helpers.h"

#define S_TO_NS 1000000000L
#define TOLERANCE_MILLISECONDS 100000000L

static void _test_timer_helper(bool use_abs_timer) {
    int efd, tfd;

    /* create new epoll/timerfd */
    assert_nonneg_errno(tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

    struct timespec start = {0};
    struct timespec end = {0};

    /* get the start time */
    assert_nonneg_errno(clock_gettime(CLOCK_MONOTONIC, &start));

    /* Timer will expire in 1 seconds, then every 1 second */
    /* if we get TIMER_ABSTIME, we want an absolute time based on current*/
    struct itimerspec t = {0};
    t.it_interval.tv_sec=1;
    if(use_abs_timer) {
        t.it_value = start;
        t.it_value.tv_sec += 1;
        assert_nonneg_errno(timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL));
    } else {
        t.it_value.tv_sec = 1;
        assert_nonneg_errno(timerfd_settime(tfd, 0, &t, NULL));
    }

    /* associate this timer with efd */
    assert_nonneg_errno(efd = epoll_create(1));
    struct epoll_event timerevent = {0};
    timerevent.events = EPOLLIN;
    assert_nonneg_errno(epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &timerevent));
    
    /* wait for the timer to expire 3 times. 1 + 1 + 1 = 3sec */
    struct epoll_event event = {0};
    for(int i = 0; i < 3; i++) {
        assert_nonneg_errno(epoll_wait(efd, &event, 1, -1));
        uint64_t expired = 0;
        assert_nonneg_errno(read(tfd, &expired, sizeof(uint64_t)));
    }

    /* get the end time */
    assert_nonneg_errno(clock_gettime(CLOCK_MONOTONIC, &end));

    /* let's make sure its within a small range (100ms) of 3 seconds */
    long diff = end.tv_nsec - start.tv_nsec;
    diff += (end.tv_sec - start.tv_sec)*S_TO_NS;
    diff -= 3*S_TO_NS;
    g_assert_cmpint(llabs(diff), <=, TOLERANCE_MILLISECONDS);

    epoll_ctl(efd, EPOLL_CTL_DEL, tfd, NULL);
    close(efd);
    close(tfd);
}

static void _test_absolute_timer() {
    _test_timer_helper(true);
}

static void _test_relative_timer() {
    _test_timer_helper(false);
}

static void _test_expired_timer_helper(int timeout_before_read) {
    int efd, tfd;
    assert_nonneg_errno(efd = epoll_create(1));

    /* create new timerfd */
    assert_nonneg_errno(tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

    /* timer will point to 5 nanoseconds after CLOCK_MONOTONIC began, which is before now. */
    struct itimerspec t = {0};
    t.it_value.tv_nsec = 5;

    /* using ABSTIME here means the timer will expire immediately */
    assert_nonneg_errno(timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL));

    /* associate this timer with an epoll efd */
    struct epoll_event timerevent = {0};
    timerevent.events = EPOLLIN;
    assert_nonneg_errno(efd = epoll_create(1));
    assert_nonneg_errno(epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &timerevent));

    /* collect the event */
    struct epoll_event event = {0};
    assert_nonneg_errno(epoll_wait(efd, &event, 1, timeout_before_read));

    /* it should have expired */
    uint64_t num_expires = 0;
    assert_nonneg_errno(read(tfd, &num_expires, sizeof(uint64_t)));
    g_assert_cmpint(num_expires, !=, 0);

    epoll_ctl(efd, EPOLL_CTL_DEL, tfd, NULL);
    close(efd);
    close(tfd);
}

static void _test_expired_timer_block() {
    _test_expired_timer_helper(-1);
}

static void _test_expired_timer_pause() {
    _test_expired_timer_helper(2);
}

static void _test_disarm_timer() {
    int efd, tfd;

    /* create new timerfd */
    assert_nonneg_errno(tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));

    /* first arm the timer to go off in 1 sec */
    struct itimerspec t = {0};
    t.it_value.tv_sec = 1;
    assert_nonneg_errno(timerfd_settime(tfd, 0, &t, NULL));

    /* reset the timer to disarm it */
    t.it_value.tv_sec = 0;
    assert_nonneg_errno(timerfd_settime(tfd, 0, &t, NULL));

    /* associate this timer with an epoll efd */
    struct epoll_event timerevent = {0};
    timerevent.events = EPOLLIN;
    assert_nonneg_errno(efd = epoll_create(1));
    assert_nonneg_errno(epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &timerevent));

    /* wait for 2 seconds */
    struct epoll_event event = {0};
    assert_nonneg_errno(epoll_wait(efd, &event, 1, 2));

    /* it should not have expired since we disarmed the 1 second timer */
    uint64_t num_expires = 0;
    int rv = read(tfd, &num_expires, sizeof(uint64_t));
    g_assert_cmpint(rv, ==, -1);
    g_assert_cmpint(errno, ==, EAGAIN);
    g_assert_cmpint(num_expires, ==, 0);

    epoll_ctl(efd, EPOLL_CTL_DEL, tfd, NULL);
    close(efd);
    close(tfd);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/timerfd/absolute", _test_absolute_timer);
    g_test_add_func("/timerfd/relative", _test_relative_timer);
    g_test_add_func("/timerfd/expired_block", _test_expired_timer_block);
    g_test_add_func("/timerfd/expired_pause", _test_expired_timer_pause);
    g_test_add_func("/timerfd/disarm", _test_disarm_timer);

    g_test_run();
    return 0;
}