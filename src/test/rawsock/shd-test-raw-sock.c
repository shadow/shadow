/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <arpa/inet.h>
#include <fcntl.h>
#define MYLOG(...) _mylog(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
int sockfd;
static void _mylog(const char* fileName, const int lineNum,
		const char* funcName, const char* format, ...) {
	struct timeval t;
	memset(&t, 0, sizeof(struct timeval));
	gettimeofday(&t, NULL);
	fprintf(stdout, "[%ld.%.06ld] [%s:%i] [%s] ", (long) t.tv_sec,
			(long) t.tv_usec, fileName, lineNum, funcName);

	va_list vargs;
	va_start(vargs, format);
	vfprintf(stdout, format, vargs);
	va_end(vargs);

	fprintf(stdout, "\n");
	fflush(stdout);
}
/* fills buffer with size random characters */
static void _fillcharbuf(char* buffer, int size) {
    for (int i = 0; i < size; i++) {
        int n = rand() % 26;
        buffer[i] = 'a' + n;
    }
}

static int _do_socket(int* fdout) {
	/* create a socket and get a socket descriptor */
	int sd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));

	MYLOG("socket() returned %i", sd);

	if (sd < 0) {
		MYLOG("socket() error was: %s", strerror(errno));
		return EXIT_FAILURE;
	}

//    if(fdout) {
	*fdout = sd;
//    }
	sockfd = sd;
	return EXIT_SUCCESS;
}

static int _do_bind(int *fd, const char *ifname) {
//	fd =sockfd;
	struct sockaddr_in bindaddr;
	memset(&bindaddr, 0, sizeof(struct sockaddr_in));
	struct sockaddr_ll ll;
	memset(&ll, 0, sizeof(ll));
	ll.sll_family = PF_PACKET;
	ll.sll_ifindex = if_nametoindex(ifname);
	ll.sll_protocol = htons(ETH_P_ALL);

	/* bind the socket to the server port */
	int result = bind(sockfd, (struct sockaddr *) &ll, sizeof(ll));

	MYLOG("bind() returned %i", result);

	if (result < 0) {
		MYLOG("bind() error was: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int _do_setsockopt(int *fd, const char *ifname) {
	struct sockaddr_in bindaddr;
	fd = sockfd;
	memset(&bindaddr, 0, sizeof(struct sockaddr_in));
	int val = 1;
	/* bind the socket to the server port */
	int result = setsockopt(sockfd, SOL_PACKET, PACKET_QDISC_BYPASS, &val,
			sizeof(val));

	MYLOG("setsockopt() returned %i", result);

	if (result < 0) {
		MYLOG("setsockopt() error was: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
#define BUFFERSIZE 20000
typedef enum _waittype {
	WAIT_WRITE, WAIT_READ
} waittype;
typedef int (*iowait_func)(int fd, waittype t);

static int _do_send(int fd, char* buf) {
	int offset = 0, amount = 0;

	/* send the bytes to the server */
	while ((amount = BUFFERSIZE - offset) > 0) {
		MYLOG("trying to send %i more bytes", amount);
		ssize_t n = send(fd, &buf[offset], (size_t) amount, 0);
		MYLOG("send() returned %li", (long )n);

		if (n < 0) {
			MYLOG("send() error was: %s", strerror(errno));
			return -1;
		} else if (n > 0) {
			MYLOG("sent %li more bytes", (long )n);
			offset += (int) n;
		} else {
			/* n == 0, on a blocking socket... */
			MYLOG(
					"unable to send to server socket %i, and send didn't block for us",
					fd);
			break;
		}
	}

	MYLOG("sent %i/%i bytes %s", offset, BUFFERSIZE,
			(offset == BUFFERSIZE) ? ":)" : ":(");
	if (offset < BUFFERSIZE) {
		MYLOG("we did not send the expected number of bytes (%i)!", BUFFERSIZE);
		return -1;
	}

	return 0;
}

static int _do_recv(int fd, char* buf) {
	int offset = 0, amount = 0;

	while ((amount = BUFFERSIZE - offset) > 0) {
		MYLOG("expecting %i more bytes, waiting for data", amount);
		ssize_t n = recv(fd, &buf[offset], (size_t) amount, 0);
		MYLOG("recv() returned %li", (long )n);

		if (n < 0) {
			MYLOG("recv() error was: %s", strerror(errno));
			return -1;
		} else if (n > 0) {
			MYLOG("got %li more bytes", (long )n);
			offset += (int) n;
		} else {
			/* n == 0, we read EOF */
			MYLOG("read EOF, server socket %i closed", fd);
			break;
		}
	}

	MYLOG("received %i/%i bytes %s", offset, BUFFERSIZE,
			(offset == BUFFERSIZE) ? ":)" : ":(");
	if (offset < BUFFERSIZE) {
		MYLOG("we did not receive the expected number of bytes (%i)!",
				BUFFERSIZE);
		return -1;
	}

	return 0;
}

/*static int _check_matching_addresses(int fd_server_listen, int fd_server_accept, int fd_client) {
 struct sockaddr_in server_listen_sockname, server_listen_peername;
 struct sockaddr_in server_accept_sockname, server_accept_peername;
 struct sockaddr_in client_sockname, client_peername;
 socklen_t addr_len = sizeof(struct sockaddr_in);

 memset(&server_listen_sockname, 0, sizeof(struct sockaddr_in));
 memset(&server_accept_sockname, 0, sizeof(struct sockaddr_in));
 memset(&client_sockname, 0, sizeof(struct sockaddr_in));
 memset(&server_listen_peername, 0, sizeof(struct sockaddr_in));
 memset(&server_accept_peername, 0, sizeof(struct sockaddr_in));
 memset(&client_peername, 0, sizeof(struct sockaddr_in));

 if(getsockname(fd_server_listen, (struct sockaddr*) &server_listen_sockname, &addr_len) < 0) {
 MYLOG("getsockname() error was: %s", strerror(errno));
 return EXIT_FAILURE;
 }

 MYLOG("found sockname %s:%i for server listen fd %i", inet_ntoa(server_listen_sockname.sin_addr),
 (int)server_listen_sockname.sin_port, fd_server_listen);

 if(getsockname(fd_server_accept, (struct sockaddr*) &server_accept_sockname, &addr_len) < 0) {
 MYLOG("getsockname() error was: %s", strerror(errno));
 return EXIT_FAILURE;
 }

 MYLOG("found sockname %s:%i for server accept fd %i", inet_ntoa(server_accept_sockname.sin_addr),
 (int)server_accept_sockname.sin_port, fd_server_accept);

 if(getsockname(fd_client, (struct sockaddr*) &client_sockname, &addr_len) < 0) {
 MYLOG("getsockname() error was: %s", strerror(errno));
 return EXIT_FAILURE;
 }

 MYLOG("found sockname %s:%i for client fd %i", inet_ntoa(client_sockname.sin_addr),
 (int)client_sockname.sin_port, fd_client);

 if(getpeername(fd_server_accept, (struct sockaddr*) &server_accept_peername, &addr_len) < 0) {
 MYLOG("getpeername() error was: %s", strerror(errno));
 return EXIT_FAILURE;
 }

 MYLOG("found peername %s:%i for server accept fd %i", inet_ntoa(server_accept_peername.sin_addr),
 (int)server_accept_peername.sin_port, fd_server_accept);

 if(getpeername(fd_client, (struct sockaddr*) &client_peername, &addr_len) < 0) {
 MYLOG("getpeername() error was: %s", strerror(errno));
 return EXIT_FAILURE;
 }

 MYLOG("found peername %s:%i for client fd %i", inet_ntoa(client_peername.sin_addr),
 (int)client_peername.sin_port, fd_client);

 /*
 * the following should hold on linux:
 *   + listener socket port == accepted socket port
 *   + accept socket port == client peer port
 *   + accept socket addr == client peer addr
 *   + client socket addr == accepted peer addr
 *   + client socket pot != accepted peer ports
 */

/*  if(server_listen_sockname.sin_port != server_accept_sockname.sin_port) {
 MYLOG("expected server listener and accepted socket ports to match but they didn't");
 return EXIT_FAILURE;
 }

 if(server_accept_sockname.sin_port != client_peername.sin_port) {
 MYLOG("expected server accepted socket port to match client peer port but they didn't");
 return EXIT_FAILURE;
 }

 if(server_accept_sockname.sin_addr.s_addr != client_peername.sin_addr.s_addr) {
 MYLOG("expected server accepted socket addr to match client peer addr but they didn't");
 return EXIT_FAILURE;
 }

 if(client_sockname.sin_addr.s_addr != server_accept_peername.sin_addr.s_addr) {
 MYLOG("expected client socket addr to match server accepted peer addr but they didn't");
 return EXIT_FAILURE;
 }

 if(client_sockname.sin_port == server_accept_peername.sin_port) {
 MYLOG("expected client socket port NOT to match server accepted peer port but they did");
 return EXIT_FAILURE;
 }

 return EXIT_SUCCESS;
 }*/

static int _test_raw_socket_client() {
	int fd1 = 0, fd2 = 0, fd3 = 0;
	struct sockaddr_in clientaddr;
	struct sockaddr_in serveraddr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));

	MYLOG("creating sockets");

	if (_do_socket(&fd1) == EXIT_FAILURE) {
		MYLOG("unable to create socket");
		return EXIT_FAILURE;
	}

	MYLOG("listening on server socket with implicit bind");

	if (_do_setsockopt(fd1, "lo") == EXIT_FAILURE) {
		MYLOG("unable to listen on server socket");
		return EXIT_FAILURE;
	}

	if (_do_bind(&fd1, "lo") == EXIT_FAILURE) {
		MYLOG("unable to bind socket");
		return EXIT_FAILURE;
	}

	char outbuf[BUFFERSIZE];
	memset(outbuf, 0, BUFFERSIZE);
	_fillcharbuf(outbuf, BUFFERSIZE);

	/* send to server */
	if (_do_send(fd1, outbuf) < 0) {
		return EXIT_FAILURE;
	}

	/* get ready to recv the response */
	char inbuf[BUFFERSIZE];
	memset(inbuf, 0, BUFFERSIZE);

	/* recv from server */
	if (_do_recv(fd1, inbuf) < 0) {
		return EXIT_FAILURE;
	}

	/* check that the buffers match */
	if (memcmp(outbuf, inbuf, BUFFERSIZE)) {
		MYLOG(
				"inconsistent message - we did not receive the same bytes that we sent :(");
		return EXIT_FAILURE;
	} else {
		/* success from our end */
		MYLOG(
				"consistent message - we received the same bytes that we sent :)");
	}

	// FIXME start
	// on ubuntu, the firewall 'ufw' blocks the remaining tests from succeeding
	// ufw auto-blocks 0.0.0.0 and 127.0.0.1, and can't seem to be made to allow it
	// so we bail out early until we have a fix
	// close(fd1);
	return EXIT_SUCCESS;
	// FIXME end

}

static int _test_raw_socket_server() {
	int fd1 = 0, fd2 = 0, fd3 = 0;
	struct sockaddr_in clientaddr;
	struct sockaddr_in serveraddr;
	socklen_t addr_len = sizeof(struct sockaddr_in);
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));

	MYLOG("creating sockets");

	if (_do_socket(&fd1) == EXIT_FAILURE) {
		MYLOG("unable to create socket");
		return EXIT_FAILURE;
	}

	MYLOG("listening on server socket with implicit bind");

	if (_do_setsockopt(fd1, "lo") == EXIT_FAILURE) {
		MYLOG("unable to listen on server socket");
		return EXIT_FAILURE;
	}

	if (_do_bind(&fd1, "lo") == EXIT_FAILURE) {
		MYLOG("unable to bind socket");
		return EXIT_FAILURE;
	}
	/* get ready to recv the response */
	char inbuf[BUFFERSIZE];
	memset(inbuf, 0, BUFFERSIZE);

	/* recv from server */
	if (_do_recv(fd1, inbuf) < 0) {
		return EXIT_FAILURE;
	}
	char outbuf[BUFFERSIZE];
	memset(outbuf, 0, BUFFERSIZE);
	_fillcharbuf(outbuf, BUFFERSIZE);

	/* send to server */
	if (_do_send(fd1, outbuf) < 0) {
		return EXIT_FAILURE;
	}

	/* check that the buffers match */
//	if (memcmp(outbuf, inbuf, BUFFERSIZE)) {
//		MYLOG(
//				"inconsistent message - we did not receive the same bytes that we sent :(");
//		return EXIT_FAILURE;
//	} else {
//		/* success from our end */
//		MYLOG(
//				"consistent message - we received the same bytes that we sent :)");
//	}

	// FIXME start
	// on ubuntu, the firewall 'ufw' blocks the remaining tests from succeeding
	// ufw auto-blocks 0.0.0.0 and 127.0.0.1, and can't seem to be made to allow it
	// so we bail out early until we have a fix
	// close(fd1);
	return EXIT_SUCCESS;
	// FIXME end

}

int main(int argc, char* argv[]) {
	printf("Starting raw sock test");
	fprintf(stdout, "########## raw socket test starting ##########\n");

	fprintf(stdout, "########## running test: _test_raw_socket()\n");
	if (strncasecmp(argv[2], "client", 5) == 0) {
		fprintf(stdout, "########## running test: _test_raw_socket_client()\n %s",argv[2]);
		if (_test_raw_socket_client() == EXIT_FAILURE) {
			fprintf(stdout,
					"########## _test_explicit_bind(SOCK_STREAM) failed\n");
			return EXIT_FAILURE;
		}
	} else if (strncasecmp(argv[2], "server", 5) == 0) {
		fprintf(stdout, "########## running test: _test_raw_socket_server()\n %s",argv[2]);

		if (_test_raw_socket_server() == EXIT_FAILURE) {
			fprintf(stdout,
					"########## _test_explicit_bind(SOCK_STREAM) failed\n");
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;
}
