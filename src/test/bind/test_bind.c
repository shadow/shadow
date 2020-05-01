/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <glib.h>

#include "test/test_glib_helpers.h"

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

static int _do_bind(int fd, in_addr_t address, in_port_t port) {
    struct sockaddr_in bindaddr;
    memset(&bindaddr, 0, sizeof(struct sockaddr_in));

    bindaddr.sin_family = AF_INET;
    bindaddr.sin_addr.s_addr = address;
    bindaddr.sin_port = port;

    /* bind the socket to the server port */
    return bind(fd, (struct sockaddr *) &bindaddr, sizeof(struct sockaddr_in));
}

static int _do_connect(int fd, struct sockaddr_in* serveraddr) {
    int count = 0;
    int saved_errno = 0;
    int result = 0;

    while (1) {
        result = connect(fd, (struct sockaddr*)serveraddr,
                         sizeof(struct sockaddr_in));
        saved_errno = errno;
        if (result == 0 || errno != EINPROGRESS) {
            break;
        }
        if (count++ > 1000) {
            g_message("waited for connect for 1 second, giving up");
            break;
        }
        g_debug("connect() returned EINPROGRESS, retrying in 1 millisecond");
        usleep((useconds_t)1000);
    }
    errno = saved_errno;
    return result;
}

static int _do_accept(int fd) {
    int count = 0;
    int result = 0;
    int saved_errno = 0;

    while (1) {
        result = accept(fd, NULL, NULL);
        saved_errno = errno;
        g_debug("accept() returned %i %s", result, strerror(errno));
        if (result >= 0 || errno != EINPROGRESS) {
            break;
        }
        if (count++ > 1000) {
            g_message("waited for accept for 1 second, giving up");
            break;
        }
        g_debug("accept() returned EINPROGRESS, retrying in 1 millisecond");
        usleep((useconds_t)1000);
    }
    errno = saved_errno;
    return result;
}

static void _test_explicit_bind(gconstpointer gp) {
    int socket_type = GPOINTER_TO_INT(gp);
    int fd1 = 0, fd2 = 0;

    g_debug("creating sockets");

    assert_true_errno((fd1 = socket(AF_INET, socket_type, 0)) >= 0);
    assert_true_errno((fd2 = socket(AF_INET, socket_type, 0)) >= 0);

    g_debug("binding one socket to localhost:11111");
    assert_true_errno(_do_bind(fd1, (in_addr_t)htonl(INADDR_LOOPBACK),
                               (in_port_t)htons(11111)) == 0);

    g_debug("try to bind the same socket again, which this should fail since we already did bind");
    g_assert_true(_do_bind(fd1, (in_addr_t)htonl(INADDR_LOOPBACK),
                           (in_port_t)htons(11111)) == -1);
    assert_errno_is(EINVAL);

    g_debug("binding a second socket to the same address as the first should fail");
    g_assert_true(_do_bind(fd2, (in_addr_t)htonl(INADDR_LOOPBACK),
                           (in_port_t)htons(11111)) == -1);
    assert_errno_is(EADDRINUSE);

    g_debug("binding a second socket to ANY with same port as the first should fail");
    g_assert_true(_do_bind(fd2, (in_addr_t)htonl(INADDR_ANY),
                           (in_port_t)htons(11111)) == -1);
    assert_errno_is(EADDRINUSE);

    g_debug("binding to 0.0.0.0:0 should succeed");
    assert_true_errno(
        _do_bind(fd2, (in_addr_t)htonl(INADDR_ANY), (in_port_t)htons(0)) == 0);

    g_debug("re-binding a socket bound to 0.0.0.0:0 should fail");
    g_assert_true(_do_bind(fd2, (in_addr_t)htonl(INADDR_ANY),
                           (in_port_t)htons(22222)) == -1);
    assert_errno_is(EINVAL);

    close(fd1);
    close(fd2);
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

static void _test_implicit_bind(gconstpointer gp) {
    int socket_type = GPOINTER_TO_INT(gp);
    int fd1 = 0, fd2 = 0, fd3 = 0;
    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    memset(&serveraddr, 0, sizeof(struct sockaddr_in));

    g_debug("creating sockets");
    assert_true_errno((fd1 = socket(AF_INET, socket_type, 0)) >= 0);
    assert_true_errno((fd2 = socket(AF_INET, socket_type, 0)) >= 0);

    g_debug("listening on server socket with implicit bind");
    assert_true_errno(listen(fd1, 0) == 0);

    assert_true_errno(
        getsockname(fd1, (struct sockaddr*)&serveraddr, &addr_len) >= 0);
    g_assert_cmpint(serveraddr.sin_addr.s_addr,==,htonl(INADDR_ANY));

    // FIXME start
    // on ubuntu, the firewall 'ufw' blocks the remaining tests from succeeding
    // ufw auto-blocks 0.0.0.0 and 127.0.0.1, and can't seem to be made to allow it
    // so we bail out early until we have a fix
    close(fd1);
    return;
    // FIXME end

    g_debug("connecting client socket to server at 0.0.0.0");
    assert_true_errno(_do_connect(fd2, &serveraddr) == 0);

    close(fd2);
    fd2 = 0;
    assert_true_errno((fd2 = socket(AF_INET, socket_type, 0)) >= 0);

    g_debug("connecting client socket to server at 127.0.0.1");
    serveraddr.sin_addr.s_addr = (in_addr_t) htonl(INADDR_LOOPBACK);
    assert_true_errno(_do_connect(fd2, &serveraddr) == 0);

    g_debug("accepting client connection");
    assert_true_errno((fd3 = _do_accept(fd1)) >= 0);

    g_debug("checking that server and client addresses match");
    g_assert_true(_check_matching_addresses(fd1, fd3, fd2) == EXIT_SUCCESS);

    close(fd1);
    close(fd2);
    close(fd3);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);

    g_test_add_data_func("/bind/explicit_bind_stream",
                         GUINT_TO_POINTER(SOCK_STREAM), &_test_explicit_bind);
    g_test_add_data_func("/bind/explicit_bind_stream_nonblock",
                         GUINT_TO_POINTER(SOCK_STREAM | SOCK_NONBLOCK),
                         &_test_explicit_bind);
    g_test_add_data_func("/bind/explicit_bind_dgram",
                         GUINT_TO_POINTER(SOCK_DGRAM), &_test_explicit_bind);
    g_test_add_data_func("/bind/explicit_bind_dgram_nonblock",
                         GUINT_TO_POINTER(SOCK_DGRAM | SOCK_NONBLOCK),
                         &_test_explicit_bind);

    g_test_add_data_func("/bind/implicit_bind_stream",
                         GUINT_TO_POINTER(SOCK_STREAM), &_test_implicit_bind);
    g_test_add_data_func("/bind/implicit_bind_stream_nonblock",
                         GUINT_TO_POINTER(SOCK_STREAM | SOCK_NONBLOCK),
                         &_test_implicit_bind);
    g_test_run();
    return EXIT_SUCCESS;
}
