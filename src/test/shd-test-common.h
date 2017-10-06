/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_TEST_SHD_TEST_COMMON_H_
#define SRC_TEST_SHD_TEST_COMMON_H_

#include <netdb.h>

/* calls common_setup_tcp_sockets and common_connect_tcp_sockets in sequence */
int common_get_connected_tcp_sockets(in_port_t server_listener_port, int* server_listener_fd_out, int* server_fd_out, int* client_fd_out);

int common_setup_tcp_sockets(int* server_listener_fd_out, int* client_fd_out, in_port_t server_listener_port);
int common_connect_tcp_sockets(int server_listener_fd, int client_fd, int* server_fd_out, in_port_t server_listener_port);

#endif /* SRC_TEST_SHD_TEST_COMMON_H_ */
