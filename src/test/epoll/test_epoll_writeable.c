/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT_STR "22222"
#define WRITE_SZ 65535
#define SERVER_HOSTNAME_STR "server"

static void _client(void) {
    printf("Start");
   struct addrinfo *client_info, hints;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   const char *port = PORT_STR;
   int rv;
   if ((rv = getaddrinfo(SERVER_HOSTNAME_STR, port, &hints, &client_info)) < 0) {
       fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
       exit(EXIT_FAILURE);
   }

   int client_socket = socket(client_info->ai_family,
                              client_info->ai_socktype,
                              client_info->ai_protocol);
   if(client_socket < 0) {
       perror("socket");
       exit(EXIT_FAILURE);
   }

   if (connect(client_socket, client_info->ai_addr, client_info->ai_addrlen) <
       0) {
       perror("connect");
       exit(EXIT_FAILURE);
   }

   char *buf = malloc(WRITE_SZ);
   int recvd = 0;
   while (recvd < 30 * WRITE_SZ) {
       int rc;
       if ((rc = recv(client_socket, buf, WRITE_SZ, 0)) < 0) {
           perror("recv");
           exit(EXIT_FAILURE);
       };
       printf("Recvd %d\n", rc);
       recvd += rc;
   }
   close(client_socket);
   printf("Exit");
}

static int _server(void) {
   struct addrinfo *info, hints;
   int rv;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   const char *port = PORT_STR;
   if ((rv = getaddrinfo(SERVER_HOSTNAME_STR, port, &hints, &info)) < 0) {
       fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
       return EXIT_FAILURE;
   }

   int server_socket = socket(info->ai_family,
                              info->ai_socktype,
                              info->ai_protocol);

   if(server_socket < 0) {
       perror("socket");
       return EXIT_FAILURE;
   }

   if (bind(server_socket, info->ai_addr, info->ai_addrlen) < 0) {
       perror("bind");
       return EXIT_FAILURE;
   }
   if (listen(server_socket, 10) < 0) {
       perror("listen");
       return EXIT_FAILURE;
   }

   struct sockaddr_storage client_addr;
   socklen_t addr_size = sizeof(client_addr);

   int client_socket =
      accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);
   if (client_socket < 0) {
       perror("accept");
       return EXIT_FAILURE;
   }

   if (fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK) < 0) {
       perror("fcntl");
       return EXIT_FAILURE;
   }

   int epoll_fd = epoll_create1(0);
   if (epoll_fd < 0) {
       perror("epoll_create1");
       return EXIT_FAILURE;
   }
   struct epoll_event ev;
   memset(&ev, 0, sizeof(ev));
   ev.data.fd = client_socket;
   ev.events = EPOLLOUT;
   if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev) < 0) {
       perror("epoll_ctl");
       return EXIT_FAILURE;
   }

   struct epoll_event events[5]; // Events to be populated by epoll_wait

   unsigned char *buf = calloc(WRITE_SZ, 1);
   if (buf == NULL) {
      printf("Could not calloc %d bytes!", WRITE_SZ);
      return EXIT_FAILURE;
   }

   int sent = 0;
   while (sent < 30 * WRITE_SZ) {
      int num_events = epoll_wait(epoll_fd, events, 5, -1);
      if (num_events < 0) {
          perror("epoll_wait");
          return EXIT_FAILURE;
      } 
      if (num_events == 0) {
          fprintf(stderr, "epoll_wait unexpectedly returned 0 events with inf timeout\n");
          return EXIT_FAILURE;
      }
      if (num_events > 0) {
         // Sanity check to ensure epoll only returned one event for the socket
         // we are monitoring.
         if (num_events > 1) {
            printf("epoll_wait returned more than 1 event but should only ret 1.\n");
            return EXIT_FAILURE;
         }

         if (events[0].data.fd != client_socket
             || !(events[0].events & EPOLLOUT))
         {
            printf("epoll_wait returned an unexpected event.\n");
            return EXIT_FAILURE;
         }

         // OK, so epoll thinks the socket is writeable.
         // Let's try to send some data.
         int rc = send(client_socket, buf, WRITE_SZ, 0);
         if (rc <= 0) {
             printf("epoll reported client_socket is writeable but send() "
                    "failed with %s.\n",
                    strerror(errno));
             return EXIT_FAILURE;
         }
         sent += rc;
      }
   }

   close(client_socket);
   close(server_socket);
   close(epoll_fd);
   return EXIT_SUCCESS;
}

int main(int argc, char **argv) {

   if (argc == 2 && strcmp(argv[1], "server_mode") == 0) {
      fprintf(stdout, "########## epoll-writeable test starting ##########\n");
      int rc = _server();
      if (rc) {
         fprintf(stdout, "########## epoll-writeable test failed! ##########\n");
         return EXIT_FAILURE;
      } else {
         fprintf(stdout, "########## epoll-writeable test passed! ##########\n");
         return EXIT_SUCCESS;
      }
   } else {
      // Start client as part of the test fixture
      _client();
   }

   return EXIT_SUCCESS;
}
