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

// Send `i` over a fifo(7) named `pipename`.
static void fifo_send_u16(const char* pipename, uint16_t i) {
    FILE* f = fopen(pipename, "w");
    assert_nonnull_errno(f);

    int count = fprintf(f, "%u\n", i);
    assert_nonneg_errno(count);
    fclose(f);
}

// Recv a u16 from a fifo(7) named `pipename`.
static uint16_t fifo_recv_u16(const char* pipename) {
    FILE* f = fopen(pipename, "r");
    assert_nonnull_errno(f);

    unsigned int i = 0;
    int count = fscanf(f, "%u\n", &i);
    assert_true_errstring(
        count == 1, feof(f) ? "Unexpected end of file" : strerror(ferror(f)));
    fclose(f);

    if (i > UINT16_MAX) {
        g_error("The uint %u was too large to be a uint16_t", i);
    }
    return i;
}

// Creates and returns a client UDP socket to localhost at `port`, and sets
// `addr` and `len` to the server address. If `port` is 0, reads the port number
// over the fifo(7) `fifo_name`.
int connect_client(struct sockaddr* addr, socklen_t* len, const char* name,
                   uint16_t port, const char* fifo_name) {
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
        (rv = getaddrinfo(name, port_string, &hints, &addrs)) == 0,
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
int connect_server(const char* name, uint16_t port, const char* fifo_name) {
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
        (rv = getaddrinfo(name, port_string, &hints, &addrs)) == 0,
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
    const char* name;
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
                           params->name, params->port, params->fifo_name);
        ssize_t sent;
        assert_nonneg_errno(sent = sendto(sock, data, sizeof(data), 0,
                                          &server_addr, server_addr_len));
        g_assert_cmpint(sent, ==, sizeof(data));
    } else {
        int sock =
            connect_server(params->name, params->port, params->fifo_name);
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

    // parse the address info of form localhost:port
    gchar** addr_parts = g_strsplit(argv[0], ":", 2);
    if (!addr_parts[0] | !addr_parts[1]) {
        g_error("The name:port argument is missing name or port");
        g_strfreev(addr_parts);
        return EXIT_FAILURE;
    }

    unsigned int port_uint = 0;
    int count = sscanf(addr_parts[1], "%u", &port_uint);
    if (count != 1 || port_uint > UINT16_MAX) {
        g_error("Error parsing port '%s'", addr_parts[1]);
        g_strfreev(addr_parts);
        return EXIT_FAILURE;
    }
    in_port_t port = port_uint;

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
        .name = addr_parts[0],
        .port = port,
        .fifo_name = fifo_name,
    };

    g_test_add_data_func("/udp/sendto_one_byte", &test_params, test_sendto_one_byte);
    g_test_run();
    g_strfreev(addr_parts);
    return EXIT_SUCCESS;
}
