/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#include "test/test_common.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int common_setup_tcp_sockets(int* server_listener_fd_out, int* client_fd_out, in_port_t* server_listener_port_out) {
    /* set up server */
    int sd = socket(AF_INET, SOCK_STREAM, 0);
    printf("socket() returned %i\n", sd);
    if (sd < 0) {
        printf("socket() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(struct sockaddr_in));
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = 0;

    int result = bind(sd, (struct sockaddr *) &saddr, sizeof(struct sockaddr_in));
    printf("bind() returned %i\n", result);
    if (result < 0) {
        printf("bind() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    socklen_t saddr_sz = sizeof(struct sockaddr_in);
    result = getsockname(sd, (struct sockaddr *) &saddr, &saddr_sz);
    if (result < 0) {
        printf("getsockname() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    *server_listener_port_out = saddr.sin_port;

    result = listen(sd, 100);
    printf("listen() returned %i\n", result);
    if (result == -1) {
        printf("listen() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* set up client */
    int cd = socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
    printf("socket() returned %i\n", cd);
    if (cd < 0) {
        printf("socket() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    if(server_listener_fd_out) {
        *server_listener_fd_out = sd;
    }
    if(client_fd_out) {
        *client_fd_out = cd;
    }
    return EXIT_SUCCESS;
}

int common_connect_tcp_sockets(int server_listener_fd, int client_fd, int* server_fd_out, in_port_t server_listener_port) {
    struct sockaddr_in caddr;
    memset(&caddr, 0, sizeof(struct sockaddr_in));
    caddr.sin_family = AF_INET;
    caddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    caddr.sin_port = server_listener_port;

    int result = connect(client_fd, (struct sockaddr *) &caddr, sizeof(struct sockaddr_in));
    printf("connect() returned %i\n", result);
    if(result < 0 && errno != EINPROGRESS) {
        printf("connect() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    result = accept(server_listener_fd, NULL, NULL);
    printf("accept() returned %i\n", result);
    if(result < 0) {
        printf("accept() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if(server_fd_out) {
        *server_fd_out = result;
    }

    result = connect(client_fd, (struct sockaddr *) &caddr, sizeof(struct sockaddr_in));
    printf("connect() returned %i\n", result);
    if(result < 0) {
        printf("connect() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int common_get_connected_tcp_sockets(int* server_listener_fd_out, int* server_fd_out, int* client_fd_out) {
    int listener, server, client;
    in_port_t port = 0;

    if(common_setup_tcp_sockets(&listener, &client, &port) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    if(common_connect_tcp_sockets(listener, client, &server, port) != EXIT_SUCCESS) {
        return EXIT_FAILURE;
    }

    if(server_listener_fd_out) {
        *server_listener_fd_out = listener;
    }
    if(server_fd_out) {
        *server_fd_out = server;
    }
    if(client_fd_out) {
        *client_fd_out = client;
    }

    return EXIT_SUCCESS;
}

bool running_in_shadow() {
    // There is the same function in the Rust tests utils code

    const char* ld_preload = getenv("LD_PRELOAD");
    if (ld_preload == NULL) {
        return false;
    }
    return strstr(ld_preload, "/proc/") != NULL;
}
