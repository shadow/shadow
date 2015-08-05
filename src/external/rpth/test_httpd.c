/*
**  GNU Pth - The GNU Portable Threads
**  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of GNU Pth, a non-preemptive thread scheduling
**  library which can be found at http://www.gnu.org/software/pth/.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
**
**  test_httpd.c: Pth test program (faked HTTP daemon)
*/
                             /* ``Unix is simple. It just takes a
                                  genius to understand its simplicity.''
                                            --- Dennis Ritchie           */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <unistd.h>

#include "pth.h"

#include "test_common.h"

/*
 * The HTTP request handler
 */

#define MAXREQLINE 1024

static void *handler(void *_arg)
{
    int fd = (int)((long)_arg);
    char caLine[MAXREQLINE];
    char str[1024];
    int n;

    /* read request */
    for (;;) {
        n = pth_readline(fd, caLine, MAXREQLINE);
        if (n < 0) {
            fprintf(stderr, "read error: errno=%d\n", errno);
            close(fd);
            return NULL;
        }
        if (n == 0)
            break;
        if (n == 1 && caLine[0] == '\n')
            break;
        caLine[n-1] = NUL;
    }

    /* simulate a little bit of processing ;) */
    pth_yield(NULL);

    /* generate response */
    sprintf(str, "HTTP/1.0 200 Ok\r\n"
                 "Server: test_httpd/%x\r\n"
                 "Connection: close\r\n"
                 "Content-type: text/plain\r\n"
                 "\r\n"
                 "Just a trivial test for GNU Pth\n"
                 "to show that it's serving data.\r\n", PTH_VERSION);
    pth_write(fd, str, strlen(str));

    /* close connection and let thread die */
    fprintf(stderr, "connection shutdown (fd: %d)\n", fd);
    close(fd);
    return NULL;
}

/*
 * A useless ticker we let run just for fun in parallel
 */

static void *ticker(void *_arg)
{
    time_t now;
    char *ct;
    float avload;

    for (;;) {
        pth_sleep(5);
        now = time(NULL);
        ct = ctime(&now);
        ct[strlen(ct)-1] = NUL;
        pth_ctrl(PTH_CTRL_GETAVLOAD, &avload);
        fprintf(stderr, "ticker woken up on %s, average load: %.2f\n",
                ct, avload);
    }
    /* NOTREACHED */
    return NULL;
}

/*
 * And the server main procedure
 */

#if defined(FD_SETSIZE)
#define REQ_MAX FD_SETSIZE-100
#else
#define REQ_MAX 100
#endif

static int s;
pth_attr_t attr;

static void myexit(int sig)
{
    close(s);
    pth_attr_destroy(attr);
    pth_kill();
    fprintf(stderr, "**Break\n");
    exit(0);
}

int main(int argc, char *argv[])
{
    struct sockaddr_in sar;
    struct protoent *pe;
    struct sockaddr_in peer_addr;
    socklen_t peer_len;
    int sr;
    int port;

    /* initialize scheduler */
    pth_init();
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  myexit);
    signal(SIGTERM, myexit);

    /* argument line parsing */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);
    if (port <= 0 || port >= 65535) {
        fprintf(stderr, "Illegal port: %d\n", port);
        exit(1);
    }

    fprintf(stderr, "This is TEST_HTTPD, a Pth test using socket I/O.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Multiple connections are accepted on the specified port.\n");
    fprintf(stderr, "For each connection a separate thread is spawned which\n");
    fprintf(stderr, "reads a HTTP request the socket and writes back a constant\n");
    fprintf(stderr, "(and useless) HTTP response to the socket.\n");
    fprintf(stderr, "Additionally a useless ticker thread awakens every 5s.\n");
    fprintf(stderr, "Watch the average scheduler load the ticker displays.\n");
    fprintf(stderr, "Hit CTRL-C for stopping this test.\n");
    fprintf(stderr, "\n");

    /* run a just for fun ticker thread */
    attr = pth_attr_new();
    pth_attr_set(attr, PTH_ATTR_NAME, "ticker");
    pth_attr_set(attr, PTH_ATTR_JOINABLE, FALSE);
    pth_attr_set(attr, PTH_ATTR_STACK_SIZE, 64*1024);
    pth_spawn(attr, ticker, NULL);

    /* create TCP socket */
    if ((pe = getprotobyname("tcp")) == NULL) {
        perror("getprotobyname");
        exit(1);
    }
    if ((s = socket(AF_INET, SOCK_STREAM, pe->p_proto)) == -1) {
        perror("socket");
        exit(1);
    }

    /* bind socket to port */
    sar.sin_family      = AF_INET;
    sar.sin_addr.s_addr = INADDR_ANY;
    sar.sin_port        = htons(port);
    if (bind(s, (struct sockaddr *)&sar, sizeof(struct sockaddr_in)) == -1) {
        perror("socket");
        exit(1);
    }

    /* start listening on the socket with a queue of 10 */
    if (listen(s, REQ_MAX) == -1) {
        perror("listen");
        exit(1);
    }

    /* finally loop for requests */
    pth_attr_set(attr, PTH_ATTR_NAME, "handler");
    fprintf(stderr, "listening on port %d (max %d simultaneous connections)\n", port, REQ_MAX);
    for (;;) {
        /* accept next connection */
        peer_len = sizeof(peer_addr);
        if ((sr = pth_accept(s, (struct sockaddr *)&peer_addr, &peer_len)) == -1) {
            perror("accept");
            pth_sleep(1);
            continue;
        }
        if (pth_ctrl(PTH_CTRL_GETTHREADS) >= REQ_MAX) {
            fprintf(stderr, "currently no more connections acceptable\n");
            continue;
        }
        fprintf(stderr, "connection established (fd: %d, ip: %s, port: %d)\n",
                sr, inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));

        /* spawn new handling thread for connection */
        pth_spawn(attr, handler, (void *)((long)sr));
    }

}

