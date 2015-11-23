/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
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

static int _test_pipe() {
    /* Create a set of pipefds
       pfd[0] == read, pfd[1] == write */
    int pfds[2];
    if(pipe(pfds) < 0) {
        fprintf(stdout, "error: pipe could not be created!\n");
        return -1;
    }

    struct epoll_event pevent;
    pevent.events = EPOLLIN;
    pevent.data.fd = pfds[0];

    int efd = epoll_create(1);
    if(epoll_ctl(efd, EPOLL_CTL_ADD, pfds[0], &pevent) < 0) {
        fprintf(stdout, "error: epoll_ctl failed");
        return -1;
    }
    
    /* First make sure there's nothing there */
    int ready = epoll_wait(efd, &pevent, 1, 100);
    if(ready < 0) {
        fprintf(stdout, "error: epoll_wait failed\n");
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }
    else if(ready > 0) {
        fprintf(stdout, "error: pipe empty but marked readable\n");
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }

    /* Now put information in pipe to be read */
    if(_test_fd_write(pfds[1]) < 0) {
        fprintf(stdout, "error: could not write to pipe\n");
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }

    /* Check again, should be something to read */
    ready = epoll_wait(efd, &pevent, 1, 100);
    if (ready != 1) {
        fprintf(stdout, "error: epoll returned %i instead of 1\n", ready);
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }

    /* Make sure we got what expected back */
    if(_test_fd_readCmp(pevent.data.fd) != 0) {
        fprintf(stdout, "error: did not read 'test' from pipe.\n");
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }

    /* success! */
    close(pfds[0]);
    close(pfds[1]);
    return 0;
}

static int _test_creat() {
    int fd = creat("testepoll.txt", 0);
    if(fd < 0) {
        fprintf(stdout, "error: could not create testepoll.txt\n");
        return -1;
    }

    /* poll will check when testpoll has info to read */
    struct epoll_event pevent;
    pevent.events = EPOLLIN;
    pevent.data.fd = fd;

    int efd = epoll_create(1);
    if(epoll_ctl(efd, EPOLL_CTL_ADD, pevent.data.fd, &pevent) == 0) {
        fprintf(stdout, "error: epoll_ctl should have failed\n");
        unlink("testepoll.txt");
        return -1;
    }

    if(errno != EPERM) {
        fprintf(stdout, "error: errno is '%i' instead of 1 (EPERM)\n",errno);
        unlink("testepoll.txt");
        return -1;
    }

    unlink("testepoll.txt");
    return 0;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## epoll test starting ##########\n");

    if(_test_pipe() < 0) {
        fprintf(stdout, "########## _test_pipe() failed\n");
        return -1;
    }

    if(_test_creat() < 0) {
        fprintf(stdout, "########## _test_creat() failed\n");
        return -1;
    }

    fprintf(stdout, "########## epoll test passed! ##########\n");
    return 0;
}
