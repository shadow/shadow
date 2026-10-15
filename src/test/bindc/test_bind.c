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

#include "lib/logger/logger.h"
#include "test/test_common.h"
#include "test/test_glib_helpers.h"

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
            info("waited for connect for 1 second, giving up");
            break;
        }
        trace("connect() returned EINPROGRESS, retrying in 1 millisecond");
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
        trace("accept() returned %i %s", result, strerror(errno));
        if (result >= 0 || errno != EAGAIN) {
            break;
        }
        if (count++ > 1000) {
            info("waited for accept for 1 second, giving up");
            break;
        }
        trace("accept() returned EINPROGRESS, retrying in 1 millisecond");
        usleep((useconds_t)1000);
    }
    errno = saved_errno;
    return result;
}

static void _test_explicit_bind(gconstpointer gp) {
    int socket_type = GPOINTER_TO_INT(gp);
    int fd1 = 0, fd2 = 0;

    trace("creating sockets");

    assert_true_errno((fd1 = socket(AF_INET, socket_type, 0)) >= 0);
    assert_true_errno((fd2 = socket(AF_INET, socket_type, 0)) >= 0);

    trace("binding one socket to localhost on ephemeral port 0");
    assert_true_errno(_do_bind(fd1, (in_addr_t)htonl(INADDR_LOOPBACK),
                               (in_port_t)htons(0)) == 0);

    // discover the assigned port so the test doesn't rely on a hardcoded port
    struct sockaddr_in bound_addr;
    socklen_t bound_len = sizeof(bound_addr);
    memset(&bound_addr, 0, sizeof(bound_addr));
    assert_true_errno(getsockname(fd1, (struct sockaddr*)&bound_addr, &bound_len) == 0);
    in_port_t assigned_port = bound_addr.sin_port;

    trace("try to bind the same socket again, which this should fail since we already did bind");
    g_assert_true(_do_bind(fd1, (in_addr_t)htonl(INADDR_LOOPBACK),
                           assigned_port) == -1);
    assert_errno_is(EINVAL);

    trace("binding a second socket to the same address as the first should fail");
    g_assert_true(_do_bind(fd2, (in_addr_t)htonl(INADDR_LOOPBACK),
                           assigned_port) == -1);
    assert_errno_is(EADDRINUSE);

    trace("binding a second socket to ANY with same port as the first should fail");
    g_assert_true(_do_bind(fd2, (in_addr_t)htonl(INADDR_ANY),
                           assigned_port) == -1);
    assert_errno_is(EADDRINUSE);

    trace("binding to 0.0.0.0:0 should succeed");
    assert_true_errno(
        _do_bind(fd2, (in_addr_t)htonl(INADDR_ANY), (in_port_t)htons(0)) == 0);

    trace("re-binding a socket bound to 0.0.0.0:0 should fail");
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

    assert_true_errno(getsockname(fd_server_listen, (struct sockaddr*) &server_listen_sockname, &addr_len) == 0);

    trace("found sockname %s:%i for server listen fd %i",
            inet_ntoa(server_listen_sockname.sin_addr),
            (int)server_listen_sockname.sin_port, fd_server_listen);

    assert_true_errno(getsockname(fd_server_accept, (struct sockaddr*) &server_accept_sockname, &addr_len) == 0);

    trace("found sockname %s:%i for server accept fd %i",
            inet_ntoa(server_accept_sockname.sin_addr),
            (int)server_accept_sockname.sin_port, fd_server_accept);

    assert_true_errno(getsockname(fd_client, (struct sockaddr*)&client_sockname,
                                  &addr_len) == 0);

    trace("found sockname %s:%i for client fd %i",
            inet_ntoa(client_sockname.sin_addr), (int)client_sockname.sin_port,
            fd_client);

    assert_true_errno(getpeername(fd_server_accept,
                                  (struct sockaddr*)&server_accept_peername,
                                  &addr_len) == 0);

    trace("found peername %s:%i for server accept fd %i",
            inet_ntoa(server_accept_peername.sin_addr),
            (int)server_accept_peername.sin_port, fd_server_accept);

    assert_true_errno(getpeername(fd_client, (struct sockaddr*)&client_peername,
                                  &addr_len) == 0);

    trace("found peername %s:%i for client fd %i",
            inet_ntoa(client_peername.sin_addr), (int)client_peername.sin_port,
            fd_client);

    /*
     * the following should hold on linux:
     *   + listener socket port == accepted socket port
     *   + accept socket port == client peer port
     *   + accept socket addr == client peer addr
     *   + client socket addr == accepted peer addr
     *   + client socket port != accepted peer ports
     */

    g_assert_cmpint(server_listen_sockname.sin_port,==,server_accept_sockname.sin_port);
    g_assert_cmpint(server_accept_sockname.sin_port,==,client_peername.sin_port);
    g_assert_cmpint(server_accept_sockname.sin_addr.s_addr,==,client_peername.sin_addr.s_addr);
    g_assert_cmpint(client_sockname.sin_addr.s_addr,==,server_accept_peername.sin_addr.s_addr);
    g_assert_cmpint(client_sockname.sin_port,==,server_accept_peername.sin_port);

    return EXIT_SUCCESS;
}

static void _test_implicit_bind(gconstpointer gp) {
    int socket_type = GPOINTER_TO_INT(gp);
    int fd1 = 0, fd2 = 0, fd3 = 0;
    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    memset(&serveraddr, 0, sizeof(struct sockaddr_in));

    trace("creating sockets");
    assert_true_errno((fd1 = socket(AF_INET, socket_type, 0)) >= 0);
    assert_true_errno((fd2 = socket(AF_INET, socket_type, 0)) >= 0);

    trace("listening on server socket with implicit bind");
    assert_true_errno(listen(fd1, 10) == 0);

    trace("checking socket address with getsockname");
    assert_true_errno(
        getsockname(fd1, (struct sockaddr*)&serveraddr, &addr_len) >= 0);
    g_assert_cmpint(serveraddr.sin_addr.s_addr,==,htonl(INADDR_ANY));

    trace("connecting client socket to server at 0.0.0.0");
    assert_true_errno(_do_connect(fd2, &serveraddr) == 0);

    trace("accepting client connection");
    assert_true_errno((fd3 = _do_accept(fd1)) >= 0);

    trace("checking that server and client addresses match");
    g_assert_true(_check_matching_addresses(fd1, fd3, fd2) == EXIT_SUCCESS);

    close(fd2);
    close(fd3);
    fd2 = 0;
    fd3 = 0;
    assert_true_errno((fd2 = socket(AF_INET, socket_type, 0)) >= 0);

    trace("connecting client socket to server at 127.0.0.1");
    serveraddr.sin_addr.s_addr = (in_addr_t) htonl(INADDR_LOOPBACK);
    assert_true_errno(_do_connect(fd2, &serveraddr) == 0);

    trace("accepting client connection");
    assert_true_errno((fd3 = _do_accept(fd1)) >= 0);

    trace("checking that server and client addresses match");
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
    return g_test_run();
}
