#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "socket.h"
#include "global.h"
#include "nbdf.h"

void usage(char * name) {
	printf("%s client <target-host> <target-port>\n%s server <listen-port>\n",name,name);
}

void server(int port) {
	socket_tp sock; int maxfd;
	socket_tp client = NULL;
	fd_set read_fds, write_fds, master_fds;
	printf("Listening on port %i.\n", port);
	
	FD_ZERO(&read_fds); FD_ZERO(&write_fds); FD_ZERO(&master_fds);
	
	sock = socket_create(SOCKET_OPTION_TCP | SOCKET_OPTION_NONBLOCK);
	if(!socket_listen(sock, port, 3)) {
		printf("Failed. Port is unavailable.\n");
		return;
	}
	maxfd = socket_getfd(sock);
	
	FD_SET(socket_getfd(sock), &master_fds);
	
	while(1) {
		read_fds = master_fds; 
		select(maxfd+1, &read_fds, &write_fds, NULL, NULL);
		
		if(FD_ISSET(socket_getfd(sock), &read_fds)) {
			client = socket_create_child(sock, SOCKET_OPTION_TCP | SOCKET_OPTION_NONBLOCK);
			maxfd = maxfd < socket_getfd(client) ? socket_getfd(client) : maxfd;
			FD_SET(socket_getfd(client), &master_fds);
			
			printf("Client connected.\n");
		}
		
		if(client && socket_isvalid(client) && FD_ISSET(socket_getfd(client), &read_fds)) {
			if(!socket_issue_read(client))
				socket_close(client);
			else if(nbdf_frame_avail(client)) {
				int a, b, c;
				char s4[20];
				char * s1, * s2, * s3;
				int i;
				int s4size = sizeof(s4);
				ptime_t pt;
				nbdf_tp nb = nbdf_import_frame(client);
				nbdf_tp nb2;
				nbdf_tp * nbs; unsigned int nbs_size;
				
				nbdf_read(nb, "SnMb", &s3, &nb2, &nbs_size, &nbs, &s4size, s4);
				printf("Read frame: %s x %i %s..\n", s3, nbs_size, s4);
				getchar();
				
				for(i=0;i<nbs_size; i++) {
					char string[1024];
					nbdf_read(nbs[i], "s", sizeof(string), string);
					printf("   - \"%s\"\n", string);
					nbdf_free(nbs[i]);
				}
				
				nbdf_read(nb2, "iStS", &a, &s1, &pt, &s2);
				printf("  Read frame: %i %s (%i:%i:%i) %s\n", a, s1, pt.v.type, pt.v.sec, pt.v.msec, s2);
				
				free(s1);
				free(s2);
				free(s3);
	
				nbdf_free(nb);
			}
		}
		
		if(client && socket_isvalid(client) && FD_ISSET(socket_getfd(client), &write_fds)) {
			if(!socket_issue_write(client))
				socket_close(client);
		}
		
		if(!socket_isvalid(client)) {
			FD_ZERO(&master_fds); FD_SET(socket_getfd(sock), &master_fds);
			
			printf("Client disconnected.\n");
			
			socket_destroy(client);
			client = NULL;
		}
		
		FD_ZERO(&write_fds);
		if(client && socket_data_outgoing(client) > 0)
			FD_SET(socket_getfd(client), &write_fds);
	}
	socket_destroy(sock);
}

void client(char * host, int port) {
	socket_tp sock;
	nbdf_tp nb, nb2;
	ptime_t pt;
	nbdf_tp nbs[3];
	
	printf("Connecting to %s on port %i... ", host, port);
	fflush(NULL);
	
	sock = socket_create(SOCKET_OPTION_TCP);
	
	if(!socket_connect(sock, host, port)) {
		printf("Failed.\n");
		return;
	}
	
	printf(" Connected.\nSending NBDF...");
	
	pt.v.type =PTIME_TYPE_VALID;
	pt.v.sec = 42; pt.v.msec = 244;
	
	nbs[0] = nbdf_construct("s","sup dawg");
	nbs[1] = nbdf_construct("s", "i love pinas");
	nbs[2] = nbdf_construct("s", "school");
		
	/* otherwise, submit an NBDF frame! */
	nb = nbdf_construct("ists", 1,"cheese", pt, "gooey");
	nb2 = nbdf_construct("snmb", "hello", nb, 3, nbs, 5, "test");
	
	/* yay */
	nbdf_send(nb2, sock);
	
	socket_close(sock);
	
	socket_destroy(sock);
	
	nbdf_free(nb);
	nbdf_free(nb2);
	
	nbdf_free(nbs[0]); nbdf_free(nbs[1]); nbdf_free(nbs[2]);
	
	printf("Sent.\n");
	
	return;
}

int main(int argc, char * argv[]) {
	/* retarded command line arg processing */
	if(argc < 2) {
		usage(argv[0]);
		return 1;
	} else if(!strcmp(argv[1],"client")) {
		if(argc != 4) {
			usage(argv[0]);
			return 1;
		}
		client(argv[2],atoi(argv[3]));
	} else if(!strcmp(argv[1],"server")) {
		if(argc != 3) {
			usage(argv[0]);
			return 1;
		}
		server(atoi(argv[2]));
	} else {
		usage(argv[0]);
		return 1;
	}
	return 0;
}
