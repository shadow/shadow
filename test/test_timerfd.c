/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>

void test_past_timer() {
  int efd = epoll_create(1);
  int tfd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);

  int retval = 0;

  struct epoll_event timerevent;
  timerevent.events = EPOLLIN;
  timerevent.data.fd = tfd;
  retval = epoll_ctl(efd, EPOLL_CTL_ADD, timerevent.data.fd, &timerevent);

  if(retval != 0) {
    perror("epoll_ctl:");
    return;
  }

  struct itimerspec t;
  t.it_value.tv_sec = 5;
  t.it_value.tv_nsec = 0;
  t.it_interval.tv_sec = 0;
  t.it_interval.tv_nsec = 0;
  retval = timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL);

  if(retval != 0) {
    perror("timerfd_settime:");
    return;
  }

  struct epoll_event event;
  int n = epoll_wait(efd, &event, 1, -1);

  uint64_t num_expires = 0;
  read(tfd, &num_expires, sizeof(uint64_t));
  printf("timer expired %lu times\n", num_expires);
  if(num_expires > 0) {
    printf("using TFD_TIMER_ABSTIME and a timer set in the past means it immediately expires\n");
  }

  close(efd);
  close(tfd);
}

void test_listen_expired_timer() {
  int efd = epoll_create(1);
  int tfd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);

  int retval = 0;

  struct itimerspec t;
  t.it_value.tv_sec = 5;
  t.it_value.tv_nsec = 0;
  t.it_interval.tv_sec = 0;
  t.it_interval.tv_nsec = 0;
  retval = timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL);

  if(retval != 0) {
    perror("timerfd_settime:");
    return;
  }

  struct epoll_event timerevent;
  timerevent.events = EPOLLIN;
  timerevent.data.fd = tfd;
  retval = epoll_ctl(efd, EPOLL_CTL_ADD, timerevent.data.fd, &timerevent);

  if(retval != 0) {
    perror("epoll_ctl:");
    return;
  }

  struct epoll_event event;
  event.data.fd = 0;
  int n = epoll_wait(efd, &event, 1, 2);
  printf("before read, %i descriptors are ready\n", n);

//  timerfd_settime(tfd, 0, &t, NULL);
//  n = epoll_wait(efd, &event, 1, 2);
//  printf("after set, %i descriptors are ready\n", n);

  uint64_t num_expires = 0;
  read(tfd, &num_expires, sizeof(uint64_t));
  printf("from read, timer expired %lu times\n", num_expires);
  if(num_expires > 0) {
    printf("an event that expires before we are listening is still reported after we start listening\n");
  }

  n = epoll_wait(efd, &event, 1, 2);
  printf("after read, %i descriptors are ready\n", n);

  close(efd);
  close(tfd);
}

void test_disarm_with_interval() {
  int tfd = timerfd_create(CLOCK_MONOTONIC, O_NONBLOCK);

  /* first arm the timer */
  struct itimerspec arm;
  arm.it_value.tv_sec = 5;
  arm.it_value.tv_nsec = 0;
  arm.it_interval.tv_sec = 2;
  arm.it_interval.tv_nsec = 2;
  timerfd_settime(tfd, TFD_TIMER_ABSTIME, &arm, NULL);

  struct itimerspec disarm;
  disarm.it_value.tv_sec = 0;
  disarm.it_value.tv_nsec = 0;
  disarm.it_interval.tv_sec = 10;
  disarm.it_interval.tv_nsec = 10;
  struct itimerspec old;
  old.it_value.tv_sec = 0;
  old.it_value.tv_nsec = 0;
  old.it_interval.tv_sec = 0;
  old.it_interval.tv_nsec = 0;
  timerfd_settime(tfd, TFD_TIMER_ABSTIME, &disarm, &old);

  printf("value: sec=%lu nsec=%lu interval: sec=%lu nsec=%lu\n",
          old.it_value.tv_sec, old.it_value.tv_nsec,
          old.it_interval.tv_sec, old.it_interval.tv_nsec);

  close(tfd);
}

int main(int argc, char* argv[]) {
  test_past_timer();
  test_listen_expired_timer();
  test_disarm_with_interval();
  return 0;
}
