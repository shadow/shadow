/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/stat.h>
#include <sys/epoll.h>
#include <errno.h>

static int _test_fd_write(int fd) {
    return write(fd, "test", 4);
}

static int _test_fd_readCmp(int fd) {
    char buf[5];
    memset(buf, '\0', 5);
    read(fd, buf, 4);
    fprintf(stdout, "read to buf: %s\n", buf);
    return(strncmp(buf, "test", 4) != 0);
}

static int _test_wait_helper(int epoll_fd, struct epoll_event* epoll_ev, int do_oneshot) {
    int total_events_reported = 0;
    int retval = 0;
    int num_iter = do_oneshot ? 5 : 1; // ONESHOT: should report 1 event even if we ask 5 times

    for (int i = 0; i < num_iter; i++) {
       /* Read up to one event with a timeout of 100ms. */
        retval = epoll_wait(epoll_fd, epoll_ev, 1, 100);

       /* error if epoll_wait failed */
       if (retval < 0) {
           fprintf(stdout, "error: epoll_wait returned error %d (%s)\n", retval, strerror(errno));
           return EXIT_FAILURE;
       } else {
           total_events_reported += retval;
       }
    }

    if (total_events_reported != 1) {
        fprintf(stdout, "error: epoll reported %i events instead of the expected 1 event\n", total_events_reported);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _test_pipe_helper(int do_oneshot) {
    /* Create a set of pipefds
       pfd[0] == read, pfd[1] == write */
    int pfds[2];
    if(pipe(pfds) < 0) {
        fprintf(stdout, "error: pipe could not be created!\n");
        return EXIT_FAILURE;
    }

    // ONESHOT: even if there is more than 1 event, we should only get one
    struct epoll_event pevent;
    pevent.events = do_oneshot ? EPOLLIN|EPOLLONESHOT : EPOLLIN;
    pevent.data.fd = pfds[0];

    int efd = epoll_create(1);
    if(epoll_ctl(efd, EPOLL_CTL_ADD, pfds[0], &pevent) < 0) {
        fprintf(stdout, "error: epoll_ctl failed\n");
        return EXIT_FAILURE;
    }

    /* First make sure there's nothing there */
    int ready = epoll_wait(efd, &pevent, 1, 100);
    if(ready < 0) {
        fprintf(stdout, "error: epoll_wait failed\n");
        close(pfds[0]);
        close(pfds[1]);
        return EXIT_FAILURE;
    }
    else if(ready > 0) {
        fprintf(stdout, "error: pipe empty but marked readable\n");
        close(pfds[0]);
        close(pfds[1]);
        return EXIT_FAILURE;
    }

    /* Now put information in pipe to be read */
    if(_test_fd_write(pfds[1]) < 0) {
        fprintf(stdout, "error: could not write to pipe\n");
        close(pfds[0]);
        close(pfds[1]);
        return EXIT_FAILURE;
    }

    /* Check again, should be something to read.
     * in both normal and oneshot cases we should only get back one event */
    if (_test_wait_helper(efd, &pevent, do_oneshot) != EXIT_SUCCESS) {
        close(pfds[0]);
        close(pfds[1]);
        return EXIT_FAILURE;
    }

    /* now if we mod, the event should be reported a second time (with or without ONESHOT) */
    pevent.events = do_oneshot ? EPOLLIN|EPOLLONESHOT : EPOLLIN;
    pevent.data.fd = pfds[0];
    if(epoll_ctl(efd, EPOLL_CTL_MOD, pfds[0], &pevent) < 0) {
        fprintf(stdout, "error: epoll_ctl failed\n");
        return EXIT_FAILURE;
    }

    if (_test_wait_helper(efd, &pevent, do_oneshot) != EXIT_SUCCESS) {
        close(pfds[0]);
        close(pfds[1]);
        return EXIT_FAILURE;
    }

    /* Make sure we got what expected back */
    if(_test_fd_readCmp(pevent.data.fd) != 0) {
        fprintf(stdout, "error: did not read 'test' from pipe.\n");
        close(pfds[0]);
        close(pfds[1]);
        return EXIT_FAILURE;
    }

    /* success! */
    close(pfds[0]);
    close(pfds[1]);
    return EXIT_SUCCESS;
}

static int _test_pipe() {
    return _test_pipe_helper(0);
}

static int _test_pipe_oneshot() {
    return _test_pipe_helper(1);
}

static int _test_creat() {
    int fd = creat("testepoll.txt", 0);
    if(fd < 0) {
        fprintf(stdout, "error: could not create testepoll.txt\n");
        return EXIT_FAILURE;
    }

    /* poll will check when testpoll has info to read */
    struct epoll_event pevent;
    pevent.events = EPOLLIN;
    pevent.data.fd = fd;

    int efd = epoll_create(1);
    if(epoll_ctl(efd, EPOLL_CTL_ADD, pevent.data.fd, &pevent) == 0) {
        fprintf(stdout, "error: epoll_ctl should have failed\n");
        unlink("testepoll.txt");
        return EXIT_FAILURE;
    }

    if(errno != EPERM) {
        fprintf(stdout, "error: errno is '%i' instead of 1 (EPERM)\n",errno);
        unlink("testepoll.txt");
        return EXIT_FAILURE;
    }

    unlink("testepoll.txt");
    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## epoll test starting ##########\n");

    fprintf(stdout, "########## _test_pipe() started\n");
    if(_test_pipe() != EXIT_SUCCESS) {
        fprintf(stdout, "########## _test_pipe() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## _test_pipe_oneshot() started\n");
    if(_test_pipe_oneshot() != EXIT_SUCCESS) {
        fprintf(stdout, "########## _test_pipe_oneshot() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## _test_creat() started\n");
    if(_test_creat() != EXIT_SUCCESS) {
        fprintf(stdout, "########## _test_creat() failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## epoll test passed! ##########\n");
    return EXIT_SUCCESS;
}
