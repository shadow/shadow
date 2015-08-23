/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/stat.h>

static int _test_fd_write(int fd) {
    return write(fd , "test", 4);
}

static int _test_fd_readCmp(int fd) {
    char buf[5];
    memset(buf, '\0', 5);
    read(fd, buf, 4);
    fprintf(stdout, "read from buf: %s\n", buf);
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

    /* poll will check when pipe has info to read */
    struct pollfd readPoll;
    readPoll.fd = pfds[0];
    readPoll.events = POLLIN;
    readPoll.revents = 0;
    
    /* First make sure there's nothing there */
    int ready = poll(&readPoll, 1, 100);
    if(ready < 0) {
        fprintf(stdout, "error: poll failed\n");
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }
    else if(ready > 0) {
        fprintf(stdout, "error: pipe marked readble. revents=%i\n", readPoll.revents);
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
    readPoll.fd = pfds[0];
    readPoll.events = POLLIN;
    readPoll.revents = 0;
    ready = poll(&readPoll, 1, 100);
    if (ready != 1) {
        fprintf(stdout, "error: poll returned %i instead of 1\n", ready);
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }

    if((readPoll.revents&POLLIN) == 0) {
        fprintf(stdout, "error: readPoll has wrong revents: %i\n", readPoll.revents);
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }

    /* Make sure we got what expected back */
    if(_test_fd_readCmp(pfds[0]) != 0) {
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
    int fd = creat("testpoll.txt", 0664);
    if(fd < 0) {
        fprintf(stdout, "error: could not create testpoll.txt\n");
        return -1;
    }

    /* poll will check when testpoll has info to read */
    struct pollfd readPoll;
    readPoll.fd = fd;
    readPoll.events = POLLIN;
    readPoll.revents = 0;

    /* First make sure there's nothing there */
    int ready = poll(&readPoll, 1, 100);
    if(ready < 0) {
        fprintf(stdout, "error: poll on empty file failed\n");
        close(fd);
        unlink("testpoll.txt");
        return -1;
    }

    /* Note: Even though the file is 0 bytes, has no data inside of it, it is still instantly
     * available for 'reading' the EOF. */
    else if(ready == 0) {
        fprintf(stdout, "error: expected EOF to be readable from empty file. revents=%i\n", readPoll.revents);
        close(fd);
        return -1;
    }

    /* write to file */
    if(_test_fd_write(fd) < 0) {
        fprintf(stdout, "error: could not write to testpoll.txt\n");
        close(fd);
        return -1;
    }

    /* Check again, should be something to read */
    //lseek(fd, 0, SEEK_SET);
    close(fd);
    fd = open("testpoll.txt", O_RDONLY);
    readPoll.fd = fd;
    readPoll.events = POLLIN;
    readPoll.revents = 0;
    ready = poll(&readPoll, 1, 100);
    if (ready != 1) {
        fprintf(stdout, "error: poll returned %i instead of 1\n", ready);
        close(fd);
        return -1;
    }

    if((readPoll.revents&POLLIN) == 0) {
        fprintf(stdout, "error: readPoll has wrong revents: %i\n", readPoll.revents);
        close(fd);
        return -1;
    }

    /* Make sure we got what expected back */
    if(_test_fd_readCmp(fd) != 0) {
        fprintf(stdout, "error: did not read 'test' from testpoll.txt.\n");
        close(fd);
        return -1;
    }

    /* success */
    close(fd);
    return 0;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## poll test starting ##########\n");

    if(_test_pipe() < 0) {
        fprintf(stdout, "########## _test_pipe() failed\n");
        return -1;
    }

    if(_test_creat() < 0) {
        fprintf(stdout, "########## _test_creat() failed\n");
        return -1;
    }

    fprintf(stdout, "########## poll test passed! ##########\n");
    return 0;
}
