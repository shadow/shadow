#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "simple_transport_lib.h"

/* my global structure to hold all variable, node-specific application state */
simple_transport_t inst;
simple_transport_tp instance = &inst;

int main(int argc, char* argv[]){
	memcpy(instance->ipstring, "127.0.0.1", 10); /* include null byte */
	instance->ip = inet_addr("127.0.0.1");

	LOG("Starting...\n");
	if(argc > 1 && strncmp(argv[1], "client", 6) == 0) {
		instance->is_server = FALSE;
		LOG("Starting client\n");

		struct sockaddr_in server;
		server.sin_family = AF_INET;
		server.sin_addr.s_addr = inet_addr("127.0.0.1");
		server.sin_port = SERVER_LISTEN_PORT;
		int sd = tcpclient_start(instance, server.sin_addr.s_addr, server.sin_port);

		int n = transport_send_message(instance, sd, &server);

		LOG("send_udp_msg returned %i\n", n);

		LOG("waiting for server response...\n");
		struct sockaddr_in source;
		while(transport_receive_message(instance, sd, (struct sockaddr*) &source) <= 0) {
			sleep(3);
		}

	} else if(argc > 1 && strncmp(argv[1], "server", 6) == 0){
		instance->is_server = TRUE;
		instance->sdata = calloc(1, sizeof(server_t));

		LOG("Starting server\n");
		int sd = tcpserver_start(instance);

		restart:
		LOG("waiting to accept client connection...\n");
		int sd_client = -1;
		while(sd_client < 0) {
			sleep(3);
			sd_client = tcpserver_accept(instance);
		}

		LOG("waiting for client message...\n");
		struct sockaddr_in client;
		while(1){
			sleep(3);
			if(transport_receive_message(instance, sd_client, (struct sockaddr*) &client) > 0){
				transport_send_message(instance, sd_client, &client);
				close(sd_client);
				goto restart;
			}
		}
		close(sd);

		free(instance->sdata);
	} else {
		LOG("Usage: %s [\"client\"|\"server\"]\n", argv[0]);
	}

	LOG("Shutdown!\n");
}
