#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "test/test_glib_helpers.h"

static void test_create_socket() {
    int sock;
    assert_nonneg_errno(sock = socket(AF_INET, SOCK_DGRAM, 0));
    assert_nonneg_errno(close(sock));
}

static void test_bind_socket() {
    int sock;
    assert_nonneg_errno(sock = socket(AF_INET, SOCK_DGRAM, 0));
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_addr = htonl(INADDR_LOOPBACK)};
    assert_nonneg_errno(bind(sock, &addr, sizeof(addr)));
    assert_nonneg_errno(close(sock));
}

static void test_getaddrinfo() {
    int sock;
    assert_nonneg_errno(sock = socket(AF_INET, SOCK_DGRAM, 0));
    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_addr = htonl(INADDR_LOOPBACK)};
    assert_nonneg_errno(bind(sock, &addr, sizeof(addr)));

    struct sockaddr_in bound_addr;
    socklen_t bound_addr_len = sizeof(bound_addr);
    assert_nonneg_errno(getsockname(sock, &bound_addr, &bound_addr_len));
    g_assert_cmpint(bound_addr_len, ==, sizeof(bound_addr));
    g_assert_cmpint(bound_addr.sin_family, ==, addr.sin_family);
    g_assert_cmpint(bound_addr.sin_addr.s_addr, ==, addr.sin_addr.s_addr);
    g_assert_cmpint(bound_addr.sin_port, !=, 0);

    assert_nonneg_errno(close(sock));
}

static void test_sendto_one_byte() {
    // Bind the server socket *first* so that the OS will buffer sent data
    // instead of just dropping it.

    int server_sock;
    assert_nonneg_errno(server_sock = socket(AF_INET, SOCK_DGRAM, 0));

    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_addr = htonl(INADDR_LOOPBACK)};
    assert_nonneg_errno(bind(server_sock, &addr, sizeof(addr)));

    socklen_t addr_len = sizeof(addr);
    assert_nonneg_errno(getsockname(server_sock, &addr, &addr_len));

    // Set up the client and send from it.

    int client_sock;
    assert_nonneg_errno(client_sock = socket(AF_INET, SOCK_DGRAM, 0));

    const char client_data[] = {42};
    ssize_t sent;
    assert_nonneg_errno(sent = sendto(client_sock, client_data,
                                      sizeof(client_data), 0, &addr,
                                      sizeof(addr)));
    g_assert_cmpint(sent, ==, sizeof(client_data));

    // Receive the data on the server end.

    char server_buf[10];
    struct sockaddr recvfrom_addr = {0};
    socklen_t recvfrom_addr_len = sizeof(recvfrom_addr);
    ssize_t recvd;
    assert_nonneg_errno(recvd = recvfrom(server_sock, server_buf,
                                         sizeof(server_buf), 0, &recvfrom_addr,
                                         &recvfrom_addr_len));
    g_assert_cmpint(recvd, ==, sizeof(client_data));
    g_assert_cmpmem(server_buf, recvd, client_data, sizeof(client_data));

    assert_nonneg_errno(close(server_sock));
    assert_nonneg_errno(close(client_sock));
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/udp_uniprocess/create_socket", test_create_socket);
    g_test_add_func("/udp_uniprocess/bind_socket", test_bind_socket);
    g_test_add_func("/udp_uniprocess/getaddrinfo", test_getaddrinfo);
    g_test_add_func("/udp_uniprocess/sendto_one_byte", test_sendto_one_byte);
    g_test_run();
    return EXIT_SUCCESS;
}
