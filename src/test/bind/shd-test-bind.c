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
#include <arpa/inet.h>
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

static int _do_listen(int fd) {
    int result = listen(fd, 0);

    MYLOG("listen() returned %i", result);

    if (result < 0) {
     MYLOG("listen() error was: %s", strerror(errno));
     return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _do_connect(int fd, struct sockaddr_in* serveraddr) {
    int count = 0;

    while(1) {
        int result = connect(fd, (struct sockaddr *) serveraddr, sizeof(struct sockaddr_in));

        MYLOG("connect() returned %i", result);

        if (result < 0 && errno == EINPROGRESS) {
            MYLOG("connect() returned EINPROGRESS, retrying in 1 millisecond");
            usleep((useconds_t)1000);

            count++;

            if(count > 1000) {
                MYLOG("waited for accept for 1 second, giving up");
                return EXIT_FAILURE;
            }

            continue;
        } else if (result < 0) {
            MYLOG("connect() error was: %s", strerror(errno));
            return EXIT_FAILURE;
        } else {
            break;
        }
    }

    return EXIT_SUCCESS;
}

static int _do_accept(int fd, int* outfd) {
    int count = 0;
    int result = 0;

    while(1) {
        result = accept(fd, NULL, NULL);

        MYLOG("accept() returned %i", result);

        if (result < 0 && errno == EINPROGRESS) {
            MYLOG("accept() returned EINPROGRESS, retrying in 1 millisecond");
            usleep((useconds_t)1000);

            count++;

            if(count > 1000) {
                MYLOG("waited for accept for 1 second, giving up");
                return EXIT_FAILURE;
            }

            continue;
        } else if (result < 0) {
            MYLOG("accept() error was: %s", strerror(errno));
            return EXIT_FAILURE;
        } else {
            break;
        }
    }

    if(outfd) {
        *outfd = result;
    }

    return EXIT_SUCCESS;
}

static int _test_explicit_bind(int socket_type) {
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

static int _check_matching_addresses(int fd_server_listen, int fd_server_accept, int fd_client) {
    struct sockaddr_in server_listen_sockname, server_listen_peername;
    struct sockaddr_in server_accept_sockname, server_accept_peername;
    struct sockaddr_in client_sockname, client_peername;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    memset(&server_listen_sockname, 0, sizeof(struct sockaddr_in));
    memset(&server_accept_sockname, 0, sizeof(struct sockaddr_in));
    memset(&client_sockname, 0, sizeof(struct sockaddr_in));
    memset(&server_listen_peername, 0, sizeof(struct sockaddr_in));
    memset(&server_accept_peername, 0, sizeof(struct sockaddr_in));
    memset(&client_peername, 0, sizeof(struct sockaddr_in));

    if(getsockname(fd_server_listen, (struct sockaddr*) &server_listen_sockname, &addr_len) < 0) {
        MYLOG("getsockname() error was: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    MYLOG("found sockname %s:%i for server listen fd %i", inet_ntoa(server_listen_sockname.sin_addr),
            (int)server_listen_sockname.sin_port, fd_server_listen);

    if(getsockname(fd_server_accept, (struct sockaddr*) &server_accept_sockname, &addr_len) < 0) {
        MYLOG("getsockname() error was: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    MYLOG("found sockname %s:%i for server accept fd %i", inet_ntoa(server_accept_sockname.sin_addr),
            (int)server_accept_sockname.sin_port, fd_server_accept);

    if(getsockname(fd_client, (struct sockaddr*) &client_sockname, &addr_len) < 0) {
        MYLOG("getsockname() error was: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    MYLOG("found sockname %s:%i for client fd %i", inet_ntoa(client_sockname.sin_addr),
            (int)client_sockname.sin_port, fd_client);

    if(getpeername(fd_server_accept, (struct sockaddr*) &server_accept_peername, &addr_len) < 0) {
        MYLOG("getpeername() error was: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    MYLOG("found peername %s:%i for server accept fd %i", inet_ntoa(server_accept_peername.sin_addr),
            (int)server_accept_peername.sin_port, fd_server_accept);

    if(getpeername(fd_client, (struct sockaddr*) &client_peername, &addr_len) < 0) {
        MYLOG("getpeername() error was: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    MYLOG("found peername %s:%i for client fd %i", inet_ntoa(client_peername.sin_addr),
            (int)client_peername.sin_port, fd_client);

    /*
     * the following should hold on linux:
     *   + listener socket port == accepted socket port
     *   + accept socket port == client peer port
     *   + accept socket addr == client peer addr
     *   + client socket addr == accepted peer addr
     *   + client socket pot != accepted peer ports
     */

    if(server_listen_sockname.sin_port != server_accept_sockname.sin_port) {
        MYLOG("expected server listener and accepted socket ports to match but they didn't");
        return EXIT_FAILURE;
    }

    if(server_accept_sockname.sin_port != client_peername.sin_port) {
        MYLOG("expected server accepted socket port to match client peer port but they didn't");
        return EXIT_FAILURE;
    }

    if(server_accept_sockname.sin_addr.s_addr != client_peername.sin_addr.s_addr) {
        MYLOG("expected server accepted socket addr to match client peer addr but they didn't");
        return EXIT_FAILURE;
    }

    if(client_sockname.sin_addr.s_addr != server_accept_peername.sin_addr.s_addr) {
        MYLOG("expected client socket addr to match server accepted peer addr but they didn't");
        return EXIT_FAILURE;
    }

    if(client_sockname.sin_port == server_accept_peername.sin_port) {
        MYLOG("expected client socket port NOT to match server accepted peer port but they did");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _test_implicit_bind(int socket_type) {
    int fd1 = 0, fd2 = 0, fd3 = 0;
    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    memset(&serveraddr, 0, sizeof(struct sockaddr_in));

    MYLOG("creating sockets");

    if(_do_socket(socket_type, &fd1) == EXIT_FAILURE) {
        MYLOG("unable to create socket");
        return EXIT_FAILURE;
    }

    if(_do_socket(socket_type, &fd2) == EXIT_FAILURE) {
        MYLOG("unable to create socket");
        return EXIT_FAILURE;
    }

    MYLOG("listening on server socket with implicit bind");

    if(_do_listen(fd1) == EXIT_FAILURE) {
        MYLOG("unable to listen on server socket");
        return EXIT_FAILURE;
    } else {
        if(getsockname(fd1, (struct sockaddr*) &serveraddr, &addr_len) < 0) {
            MYLOG("getsockname() error was: %s", strerror(errno));
            return EXIT_FAILURE;
        }
    }

    if(serveraddr.sin_addr.s_addr != htonl(INADDR_ANY)) {
        MYLOG("unexpected behavior, server socket was not implicitly bound to 0.0.0.0");
        return EXIT_FAILURE;
    }

    MYLOG("connecting client socket to server at 0.0.0.0");

    if(_do_connect(fd2, &serveraddr) == EXIT_FAILURE) {
        MYLOG("unexpected behavior, client should be able to connect to 0.0.0.0");
        return EXIT_FAILURE;
    }

    close(fd2);
    fd2 = 0;
    if(_do_socket(socket_type, &fd2) == EXIT_FAILURE) {
        MYLOG("unable to create socket");
        return EXIT_FAILURE;
    }

    MYLOG("connecting client socket to server at 127.0.0.1");

    serveraddr.sin_addr.s_addr = (in_addr_t) htonl(INADDR_LOOPBACK);

    if(_do_connect(fd2, &serveraddr) == EXIT_FAILURE) {
        MYLOG("unable to connect to server at 127.0.0.1:%i", (int)ntohs(serveraddr.sin_port));
        return EXIT_FAILURE;
    }

    MYLOG("accepting client connection");

    if(_do_accept(fd1, &fd3) == EXIT_FAILURE) {
        MYLOG("unable to accept client connection");
        return EXIT_FAILURE;
    }

    MYLOG("checking that server and client addresses match");

    if(_check_matching_addresses(fd1, fd3, fd2) == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    close(fd1);
    close(fd2);
    close(fd3);

    return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
    fprintf(stdout, "########## bind test starting ##########\n");

    fprintf(stdout, "########## running test: _test_explicit_bind(SOCK_STREAM)\n");

    if(_test_explicit_bind(SOCK_STREAM) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_explicit_bind(SOCK_STREAM) failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## running test: _test_explicit_bind(SOCK_STREAM|SOCK_NONBLOCK)\n");

    if(_test_explicit_bind(SOCK_STREAM|SOCK_NONBLOCK) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_explicit_bind(SOCK_STREAM|SOCK_NONBLOCK) failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## running test: _test_explicit_bind(SOCK_DGRAM)\n");

    if(_test_explicit_bind(SOCK_DGRAM) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_explicit_bind(SOCK_DGRAM) failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## running test: _test_explicit_bind(SOCK_DGRAM|SOCK_NONBLOCK)\n");

    if(_test_explicit_bind(SOCK_DGRAM|SOCK_NONBLOCK) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_explicit_bind(SOCK_DGRAM|SOCK_NONBLOCK) failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## running test: _test_implicit_bind(SOCK_STREAM)\n");

    if(_test_implicit_bind(SOCK_STREAM) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_implicit_bind(SOCK_STREAM) failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## running test: _test_implicit_bind(SOCK_STREAM|SOCK_NONBLOCK)\n");

    if(_test_implicit_bind(SOCK_STREAM|SOCK_NONBLOCK) == EXIT_FAILURE) {
        fprintf(stdout, "########## _test_implicit_bind(SOCK_STREAM|SOCK_NONBLOCK) failed\n");
        return EXIT_FAILURE;
    }

    fprintf(stdout, "########## bind test passed! ##########\n");

    return EXIT_SUCCESS;
}
