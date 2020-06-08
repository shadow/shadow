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
#include <string.h>
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

static void _udp_socketpair(int* client, int* server,
                            struct sockaddr_in* addr) {
    assert_nonneg_errno(*server = socket(AF_INET, SOCK_DGRAM, 0));

    *addr = (struct sockaddr_in){.sin_family = AF_INET,
                                 .sin_addr = htonl(INADDR_LOOPBACK)};
    assert_nonneg_errno(bind(*server, addr, sizeof(*addr)));

    socklen_t addr_len = sizeof(*addr);
    assert_nonneg_errno(getsockname(*server, addr, &addr_len));

    assert_nonneg_errno(*client = socket(AF_INET, SOCK_DGRAM, 0));
}

static void test_sendto_one_byte() {
    int client_sock, server_sock;
    struct sockaddr_in addr = {0};
    _udp_socketpair(&client_sock, &server_sock, &addr);

    const char client_data[] = {42};
    ssize_t sent;
    assert_nonneg_errno(sent = sendto(client_sock, client_data,
                                      sizeof(client_data), 0, &addr,
                                      sizeof(addr)));
    g_assert_cmpint(sent, ==, sizeof(client_data));

    char server_buf[10];
    struct sockaddr recvfrom_addr = {0};
    socklen_t recvfrom_addr_len = sizeof(recvfrom_addr);
    ssize_t recvd;
    assert_nonneg_errno(recvd = recvfrom(server_sock, server_buf,
                                         sizeof(server_buf), 0, &recvfrom_addr,
                                         &recvfrom_addr_len));
    g_assert_cmpmem(server_buf, recvd, client_data, sizeof(client_data));

    assert_nonneg_errno(close(server_sock));
    assert_nonneg_errno(close(client_sock));
}

static void test_echo() {
    int client_sock, server_sock;
    struct sockaddr_in addr = {0};
    _udp_socketpair(&client_sock, &server_sock, &addr);

    char client_send_buf[1024];
    memset(client_send_buf, 42, sizeof(client_send_buf));

    {
        ssize_t sent;
        assert_nonneg_errno(sent = sendto(client_sock, client_send_buf,
                                          sizeof(client_send_buf), 0, &addr,
                                          sizeof(addr)));
        g_assert_cmpint(sent, ==, sizeof(client_send_buf));
    }

    char server_buf[sizeof(client_send_buf)];
    struct sockaddr recvfrom_addr = {0};
    socklen_t recvfrom_addr_len = sizeof(recvfrom_addr);
    {
        ssize_t recvd;
        assert_nonneg_errno(
            recvd = recvfrom(server_sock, server_buf, sizeof(server_buf), 0,
                             &recvfrom_addr, &recvfrom_addr_len));
        g_assert_cmpmem(server_buf, recvd, client_send_buf,
                        sizeof(client_send_buf));
    }

    {
        ssize_t sent;
        assert_nonneg_errno(sent = sendto(server_sock, server_buf,
                                          sizeof(server_buf), 0, &recvfrom_addr,
                                          recvfrom_addr_len));
        g_assert_cmpint(sent, ==, sizeof(client_send_buf));
    }

    char client_recv_buf[sizeof(client_send_buf)];
    {
        ssize_t recvd;
        assert_nonneg_errno(recvd = recvfrom(client_sock, client_recv_buf,
                                             sizeof(client_recv_buf), 0, NULL,
                                             NULL));
        g_assert_cmpmem(client_recv_buf, recvd, client_send_buf,
                        sizeof(client_send_buf));
    }

    assert_nonneg_errno(close(server_sock));
    assert_nonneg_errno(close(client_sock));
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/udp_uniprocess/create_socket", test_create_socket);
    g_test_add_func("/udp_uniprocess/bind_socket", test_bind_socket);
    g_test_add_func("/udp_uniprocess/getaddrinfo", test_getaddrinfo);
    g_test_add_func("/udp_uniprocess/sendto_one_byte", test_sendto_one_byte);
    g_test_add_func("/udp_uniprocess/echo", test_echo);
    g_test_run();
    return EXIT_SUCCESS;
}
