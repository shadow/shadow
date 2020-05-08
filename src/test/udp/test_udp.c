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

// Send `i` over a fifo(7) named `pipename`.
static void fifo_send_u16(const char* pipename, uint16_t i) {
    FILE* f = fopen(pipename, "w");
    assert_nonnull_errno(f);

    char buf[10];
    int len = snprintf(buf, 10, "%d", i);
    g_assert(len < sizeof(buf));

    int count = fwrite(buf, 1, len, f);
    assert_true_errno(count == len);
    fclose(f);
}

// Recv a u16 from a fifo(7) named `pipename`.
static uint16_t fifo_recv_u16(const char* pipename) {
    FILE* f = fopen(pipename, "r");
    assert_nonnull_errno(f);
    char buf[10] = {0};
    int count = fread(buf, 1, sizeof(buf)-1, f);
    assert_true_errstring(count != 0, feof(f) ? "Unexpected end of file"
                                              : strerror(ferror(f)));
    fclose(f);

    guint64 val;
    GError *error = NULL;
    g_ascii_string_to_unsigned(buf, /* base= */ 10, /* min= */ 0,
                                    /* max= */ UINT16_MAX, &val, &error);
    g_assert_no_error(error);
    return val;
}

// Creates and returns a client UDP socket to localhost at `port`, and sets
// `addr` and `len` to the server address. If `port` is 0, reads the port number
// over the fifo(7) `fifo_name`.
int connect_client(struct sockaddr* addr, socklen_t* len, uint16_t port,
                   const char* fifo_name) {
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
    };
    if (port == 0) {
        port = fifo_recv_u16(fifo_name);
    }
    gchar* port_string = g_strdup_printf("%u", port);
    struct addrinfo* addrs = NULL;
    int rv;
    assert_true_errstring(
        (rv = getaddrinfo(NULL, port_string, &hints, &addrs)) == 0,
        gai_strerror(rv));
    g_assert_nonnull(addrs);
    g_assert_cmpint(addrs->ai_addrlen, <=, *len);
    *addr = *addrs->ai_addr;
    *len = addrs->ai_addrlen;
    freeaddrinfo(addrs);

    int sock;
    assert_nonneg_errno(sock = socket(AF_INET, SOCK_DGRAM, 0));
    return sock;
}

// Creates and returns a UDP listening on `port`. If `port` is 0, uses an
// automatically assigned port number and writes it to the fifo(7) `fifo_name`.
int connect_server(uint16_t port, const char* fifo_name) {
    int sock;
    assert_nonneg_errno(sock = socket(AF_INET, SOCK_DGRAM, 0));
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
        .ai_flags = AI_PASSIVE,
    };
    gchar* port_string = g_strdup_printf("%u", port);
    struct addrinfo* addrs = NULL;
    int rv;
    assert_true_errstring(
        (rv = getaddrinfo(NULL, port_string, &hints, &addrs)) == 0,
        gai_strerror(rv));
    g_free(port_string);
    port_string =  NULL;
    g_assert_nonnull(addrs);
    assert_nonneg_errno(bind(sock, addrs->ai_addr, addrs->ai_addrlen));
    freeaddrinfo(addrs);
    addrs = NULL;
    if (port == 0) {
        // Tell the client side what port we ended up with.
        struct sockaddr_in bound_addr;
        socklen_t bound_addr_len = sizeof(bound_addr);
        assert_nonneg_errno(getsockname(sock, &bound_addr, &bound_addr_len));
        g_assert(bound_addr_len <= sizeof(bound_addr));
        fifo_send_u16(fifo_name, ntohs(bound_addr.sin_port));
    }
    return sock;
}

typedef enum { CLIENT, SERVER } Type;
typedef struct {
    Type type;
    uint16_t port;
    const char* fifo_name;
} TestParams;

void test_sendto_one_byte(const void* void_params) {
    const TestParams* params = void_params;
    const char data[] = {42};
    if (params->type == CLIENT) {
        struct sockaddr_in server_addr = {};
        socklen_t server_addr_len = sizeof(server_addr);
        int sock =
            connect_client((struct sockaddr*)&server_addr, &server_addr_len,
                           params->port, params->fifo_name);
        ssize_t sent;
        assert_nonneg_errno(sent = sendto(sock, data, sizeof(data), 0,
                                          &server_addr, server_addr_len));
        g_assert_cmpint(sent, ==, sizeof(data));
    } else {
        int sock = connect_server(params->port, params->fifo_name);
        char recv_buf[10];
        struct sockaddr recvfrom_addr = {};
        socklen_t recvfrom_addr_len = sizeof(recvfrom_addr);
        ssize_t recvd;
        assert_nonneg_errno(recvd =
                                recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                                         &recvfrom_addr, &recvfrom_addr_len));
        g_assert_cmpmem(recv_buf, recvd, data, sizeof(data));
        /*
        g_assert_cmpmem(&recvfrom_addr, recvfrom_addr_len, &server_addr,
                        server_addr_len);
                        */
    }
}

int main(int argc, char* argv[]) {
    // Parses any arguments that glib understands and strips them out.
    // We tell it *not* to set the program name because we do that ourselves
    // later, and it may only be done once.
    g_test_init(&argc, &argv, "no_g_set_prgname", NULL);

    // Consume first argument (program name)
    const char* binname = argv[0];
    --argc;
    ++argv;

    // Get type (server|client)
    if(argc < 1) {
        g_error("Missing type name");
    }
    Type type;
    if(!g_strcmp0(argv[0], "client")) {
        type = CLIENT;
    } else if (!g_strcmp0(argv[0], "server")) {
        type = SERVER;
    } else {
        g_error("Bad type name: %s", argv[0]);
    }
    char *prgname = g_strdup_printf("%s:%s", binname, argv[0]);
    g_set_prgname(prgname);
    g_free(prgname);
    --argc;
    ++argv;

    // Get port number to use, or 0 to use dynamic assignment.
    if(argc < 1) {
        g_error("Missing port number");
        return EXIT_FAILURE;
    }
    guint64 port64;
    GError* error = NULL;
    if (!g_ascii_string_to_unsigned(argv[0], /* base= */ 10, /* min= */ 0,
                                    /* max= */ UINT16_MAX, &port64, &error)) {
        g_error("Parsing port '%s': %s", argv[1], error->message);
        return EXIT_FAILURE;
    }
    in_port_t port = port64;
    --argc;
    ++argv;

    // If port is zero, get the fifo to use to communicate the port.
    const char* fifo_name = NULL;
    if (port == 0) {
        if (argc < 1) {
            g_error("Missing fifo name");
        }
        fifo_name = argv[0];
        --argc;
        ++argv;
    }

    TestParams test_params = {
        .type = type,
        .port = port,
        .fifo_name = fifo_name,
    };

    g_test_add_data_func("/udp/sendto_one_byte", &test_params, test_sendto_one_byte);
    g_test_run();
    return EXIT_SUCCESS;
}
