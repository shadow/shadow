#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <string.h>

#define S_TO_NS 1000000000L

static int _test_normal(int flags) {

    /* create new epoll/timerfd */
    int efd = epoll_create(1);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    struct epoll_event timerevent;
    timerevent.events = EPOLLIN;
    timerevent.data.fd = tfd;

    /* associate this timer with efd */
    if(epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &timerevent) < 0) {
        return -1;
    }

    struct timespec start, end;

    /* get the start time */
    if(clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
        return -1;
    }

    /* Timer will expire at 3 seconds, then every 1 second */
    /* if we get TIMER_ABSTIME, we want an absolute time based on current*/
    struct itimerspec t;
    if(flags == TFD_TIMER_ABSTIME) {
        t.it_value = start;
        t.it_value.tv_sec += 1;
    }
    else {
        t.it_value.tv_sec = 1;
        t.it_value.tv_nsec = 0;
    }
    t.it_interval.tv_sec=1;
    t.it_interval.tv_nsec=0;

    if(timerfd_settime(tfd, flags, &t, NULL) != 0) {
        return -1;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(struct epoll_event));

    /* wait for the timer to expire 3 times. 1 + 1 + 1 = 3sec */
    for(int i = 0; i < 3; i++) {
        epoll_wait(efd, &event, 1, -1);
        uint64_t expired = 0;
        read(tfd, &expired, sizeof(uint64_t));
    }

    /* get the end time */
    if(clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
        return -1;
    }

    /* let's make sure its within a small range (100ms) of 3 seconds */
    long diff = end.tv_nsec - start.tv_nsec;
    diff += (end.tv_sec - start.tv_sec)*S_TO_NS;
    diff -= 3*S_TO_NS;
    if( llabs(diff) > 100000000) {
        fprintf(stdout, "error: timer failed. diff: %li\n", diff);
        return -1;
    }

    epoll_ctl(efd, EPOLL_CTL_DEL, tfd, NULL);
    close(efd);
    close(tfd);

    /* success! */
    return 0;
}

static int _test_late_timer() {

    /* create new epoll/timerfd */
    int efd = epoll_create(1);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    struct epoll_event timerevent;
    timerevent.events = EPOLLIN;
    timerevent.data.fd = tfd;

    /* associate this timer with efd */
    if(epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &timerevent) != 0) {
        return -1;
    }

    /* timer will point to 5 seconds after CLOCK_MONOTONIC began, which has passed */
    struct itimerspec t;
    t.it_value.tv_sec = 5;
    t.it_value.tv_nsec = 0;
    t.it_interval.tv_sec = 0;
    t.it_interval.tv_nsec = 0;

    if(timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL) != 0){ 
        return -1;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(struct epoll_event));
    epoll_wait(efd, &event, 1, -1);

    uint64_t num_expires = 0;
    read(tfd, &num_expires, sizeof(uint64_t));

    epoll_ctl(efd, EPOLL_CTL_DEL, tfd, NULL);
    close(efd);
    close(tfd);

    if(num_expires == 0) {
        fprintf(stdout, "error: timer did not expire when set to past");
        return -1;
    }

    /* success! */
    return 0;
}

static int _test_expired_timer() {

    /* create new epoll/timerfd */
    int efd = epoll_create(1);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    struct itimerspec t;
    t.it_value.tv_sec = 5;
    t.it_value.tv_nsec = 0;
    t.it_interval.tv_sec = 0;
    t.it_interval.tv_nsec = 0;

    /* as shown in last test, timer will be expired immediately */
    if (timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL) != 0) {
        return -1;
    }

    struct epoll_event timerevent;
    timerevent.events = EPOLLIN;
    timerevent.data.fd = tfd;

    /* associate this timer with efd */
    if (epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &timerevent) != 0) {
        return -1;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(struct epoll_event));
    epoll_wait(efd, &event, 1, 2);

    uint64_t num_expires = 0;
    read(tfd, &num_expires, sizeof(uint64_t));

    epoll_ctl(efd, EPOLL_CTL_DEL, tfd, NULL);
    close(efd);
    close(tfd);

    if(num_expires == 0) {
        fprintf(stdout, "error: timer was not expired on late read");
        return -1;
    }

    /* success! */
    return 0;
}

static int _test_disarm_timer() {

    /* create new epoll/timerfd */
    int efd = epoll_create(1);
    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    /* first arm the timer to go off in 3 sec */
    struct itimerspec t;
    t.it_value.tv_sec = 3;
    t.it_value.tv_nsec = 0;
    t.it_interval.tv_sec = 0;
    t.it_interval.tv_nsec = 0;
    if (timerfd_settime(tfd, 0, &t, NULL) != 0) {
        return -1;
    }

    /* reset the timer to disarm it */
    t.it_value.tv_sec = 0;
    if (timerfd_settime(tfd, 0, &t, NULL) != 0) {
        return -1;
    }

    struct epoll_event timerevent;
    timerevent.events = EPOLLIN;
    timerevent.data.fd = tfd;

    /* associate this timer with efd */
    if (epoll_ctl(efd, EPOLL_CTL_ADD, tfd, &timerevent) != 0) {
        return -1;
    }

    struct epoll_event event;
    memset(&event, 0, sizeof(struct epoll_event));
    epoll_wait(efd, &event, 1, 2);

    uint64_t num_expires = 0;
    read(tfd, &num_expires, sizeof(uint64_t));

    epoll_ctl(efd, EPOLL_CTL_DEL, tfd, NULL);
    close(efd);
    close(tfd);

    if(num_expires != 0) {
        fprintf(stdout, "error: timer expired after it was disarmed");
        return -1;
    }

    /* success! */
    return 0;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## timerfd_epoll test starting ##########\n");

    if(_test_normal(0) < 0) {
        fprintf(stdout, "########## _test_normal(0) failed\n");
        return -1;
    }

    if(_test_normal(TFD_TIMER_ABSTIME) < 0) {
        fprintf(stdout, "########## _test_normal(TFD_TIMER_ABSTIME) failed\n");
        return -1;
    }

    if(_test_late_timer() < 0) {
        fprintf(stdout, "########## _test_late_timer failed\n");
        return -1;
    }

    if(_test_expired_timer() < 0) {
        fprintf(stdout, "########## _test_expired_timer failed\n");
        return -1;
    }

    if(_test_disarm_timer() < 0) {
        fprintf(stdout, "########## _test_disarm_timer failed\n");
        return -1;
    }

    fprintf(stdout, "########## timerfd_epoll test passed! ##########\n");
    return 0;
}
