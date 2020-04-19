/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */
#define _POSIX_C_SOURCE 199309L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define PORT_STR "22222"
#define WRITE_SZ 65535
#define TARGET_NODE "testnode"

static void _client(void) {
   char buf[WRITE_SZ];
   struct addrinfo *client_info, hints;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;

   const char *port = PORT_STR;
   getaddrinfo(TARGET_NODE, port, &hints, &client_info);

   int client_socket = socket(client_info->ai_family,
                              client_info->ai_socktype,
                              client_info->ai_protocol);

   connect(client_socket, client_info->ai_addr, client_info->ai_addrlen);

   // Read messages until the server stops the communication
   while (0 < read(client_socket, buf, sizeof(buf)));
}

static int _server(void) {
   struct addrinfo *info, hints;

   memset(&hints, 0, sizeof(hints));
   hints.ai_family = AF_INET;
   hints.ai_socktype = SOCK_STREAM;
   hints.ai_flags = AI_PASSIVE;

   const char *port = PORT_STR;
   getaddrinfo(NULL, port, &hints, &info);

   int server_socket = socket(info->ai_family,
                              info->ai_socktype,
                              info->ai_protocol);

   bind(server_socket, info->ai_addr, info->ai_addrlen);
   listen(server_socket, 10);

   struct sockaddr_storage client_addr;
   socklen_t addr_size = sizeof(client_addr);

   int client_socket =
      accept(server_socket, (struct sockaddr *)&client_addr, &addr_size);

   fcntl(client_socket, F_SETFL, fcntl(client_socket, F_GETFL, 0) | O_NONBLOCK);

   int epoll_fd = epoll_create1(0);
   struct epoll_event ev;
   memset(&ev, 0, sizeof(ev));
   ev.data.fd = client_socket;
   ev.events = EPOLLOUT;
   epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev);

   struct epoll_event events[5]; // Events to be populated by epoll_wait

   unsigned char *buf = calloc(WRITE_SZ, 1);
   if (buf == NULL) {
      printf("Could not calloc %d bytes!", WRITE_SZ);
      return EXIT_FAILURE;
   }

   for (int idx = 0; idx < 30; ++idx) {
      int num_events = epoll_wait(epoll_fd, events, 5, -1);

      if (num_events == -1) {
          printf("epoll_wait failed and returns -1.\n");
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
            printf("epoll reported client_socket is writeable but send() failed with %s.\n", strerror(errno));
            return EXIT_FAILURE;
         }
      }
   }

   // Close the socket to allow client to stop running
   close(client_socket);

   return EXIT_SUCCESS;
}

int main(int argc, char **argv) {

   if (argc == 2 && strcmp(argv[1], "server") == 0) {
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
