/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

#define MYLOG(...) _mylog(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

static void _mylog(const char* fileName, const int lineNum, const char* funcName, const char* format, ...) {
    struct timeval t;
    memset(&t, 0, sizeof(struct timeval));
    gettimeofday(&t, NULL);
    fprintf(stdout, "[%ld.%.06ld] [%s:%i] [%s] ", (long)t.tv_sec, (long)t.tv_usec, fileName, lineNum, funcName);

    va_list vargs;
    va_start(vargs, format);
    vfprintf(stdout, format, vargs);
    va_end(vargs);

    fprintf(stdout, "\n");
    fflush(stdout);
}

static int _do_socket(int type, int* fdout) {
    /* create a socket and get a socket descriptor */
    int sd = socket(AF_INET, type, 0);

    MYLOG("socket() returned %i", sd);

    if (sd < 0) {
        MYLOG("socket() error was: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    if(fdout) {
        *fdout = sd;
    }

    return EXIT_SUCCESS;
}

static int _do_bind(int fd, in_addr_t address, in_port_t port) {
    struct sockaddr_in bindaddr;
    memset(&bindaddr, 0, sizeof(struct sockaddr_in));

    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = address;
    bindaddr.sin_port = port;

    /* bind the socket to the server port */
    int result = bind(fd, (struct sockaddr *) &bindaddr, sizeof(struct sockaddr_in));

    MYLOG("bind() returned %i", result);

    if (result < 0) {
        MYLOG("bind() error was: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _test_bind(int socket_type) {
    int fd1 = 0, fd2 = 0;

    MYLOG("creating sockets");

    if(_do_socket(socket_type, &fd1) == EXIT_FAILURE) {
        MYLOG("unable to create socket");
        return EXIT_FAILURE;
    }

    if(_do_socket(socket_type, &fd2) == EXIT_FAILURE) {
        MYLOG("unable to create socket");
        return EXIT_FAILURE;
    }

    MYLOG("binding one socket to localhost:11111");

    if(_do_bind(fd1, (in_addr_t) htonl(INADDR_LOOPBACK), (in_port_t) htons(11111)) == EXIT_FAILURE) {
        MYLOG("unable to bind new socket to localhost:11111");
        return EXIT_FAILURE;
    }

    MYLOG("try to bind the same socket again, which this should fail since we already did bind");

    if(_do_bind(fd1, (in_addr_t) htonl(INADDR_LOOPBACK), (in_port_t) htons(11111)) == EXIT_SUCCESS) {
        MYLOG("unexpected behavior, binding LOOPBACK socket twice succeeded");
        return EXIT_FAILURE;
    } else if(errno != EINVAL) {
        MYLOG("unexpected behavior, binding LOOPBACK socket twice failed with errno %i but we expected %i (EINVAL)",
                errno, EINVAL);
        return EXIT_FAILURE;
    }

    MYLOG("binding a second socket to the same address as the first should fail");

    if(_do_bind(fd2, (in_addr_t) htonl(INADDR_LOOPBACK), (in_port_t) htons(11111)) == EXIT_SUCCESS) {
        MYLOG("unexpected behavior, binding two sockets to the same LOOPBACK address succeeded");
        return EXIT_FAILURE;
    } else if(errno != EADDRINUSE) {
        MYLOG("unexpected behavior, binding two sockets to the same LOOPBACK address failed with errno %i but we expected %i (EINVAL)",
                errno, EADDRINUSE);
        return EXIT_FAILURE;
    }

    MYLOG("binding a second socket to ANY with same port as the first should fail");

    if(_do_bind(fd2, (in_addr_t) htonl(INADDR_ANY), (in_port_t) htons(11111)) == EXIT_SUCCESS) {
        MYLOG("unexpected behavior, binding two sockets to LOOPBACK:11111 and ANY:11111 succeeded");
        return EXIT_FAILURE;
    } else if(errno != EADDRINUSE) {
        MYLOG("unexpected behavior, binding two sockets to LOOPBACK:11111 and ANY:11111 failed with errno %i but we expected %i (EADDRINUSE)",
                errno, EADDRINUSE);
        return EXIT_FAILURE;
    }

    MYLOG("binding to 0.0.0.0:0 should succeed");

    if(_do_bind(fd2, (in_addr_t) htonl(INADDR_ANY), (in_port_t) htons(0)) == EXIT_FAILURE) {
        MYLOG("unable to bind to ANY:0");
        return EXIT_FAILURE;
    }

    MYLOG("re-binding a socket bound to 0.0.0.0:0 should fail");

    if(_do_bind(fd2, (in_addr_t) htonl(INADDR_ANY), (in_port_t) htons(22222)) == EXIT_SUCCESS) {
        MYLOG("unexpected behavior, binding a socket to ANY:0 and then ANY:22222 succeeded");
        return EXIT_FAILURE;
    } else if(errno != EINVAL) {
        MYLOG("unexpected behavior, binding socket to ANY:0 and then ANY:22222 failed with errno %i but we expected %i (EINVAL)",
                errno, EINVAL);
        return EXIT_FAILURE;
    }

    close(fd1);
    close(fd2);

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## bind test starting ##########\n");

    if(_test_bind(SOCK_STREAM) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_bind(SOCK_STREAM) failed\n");
        return EXIT_FAILURE;
    }

    if(_test_bind(SOCK_STREAM|SOCK_NONBLOCK) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_bind(SOCK_STREAM|SOCK_NONBLOCK) failed\n");
        return EXIT_FAILURE;
    }

    if(_test_bind(SOCK_DGRAM) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_bind(SOCK_DGRAM) failed\n");
        return EXIT_FAILURE;
    }

    if(_test_bind(SOCK_DGRAM|SOCK_NONBLOCK) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_bind(SOCK_DGRAM|SOCK_NONBLOCK) failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## bind test passed! ##########\n");

    return EXIT_SUCCESS;
}
