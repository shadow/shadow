/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/select.h>

#define USAGE "USAGE: 'shd-test-tcp iomode type'; iomode=('blocking'|'nonblocking-poll'|'nonblocking-epoll'|'nonblocking-select') type=('client' server_ip|'server'|'loopback')"
#define MYLOG(...) _mylog(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define SERVER_PORT 58333
#define BUFFERSIZE 20000

typedef int (*iowait_func)(int fd, int isread);

static void _mylog(const char* fileName, const int lineNum, const char* funcName, const char* format, ...) {
    //fprintf(stdout, "[%.010lu] [%s:%i] [%s] ", (ulong)time(NULL), fileName, lineNum, funcName);
    struct timespec t;
    memset(&t, 0, sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, &t);
    fprintf(stdout, "[%lu.%.09lu] ", (ulong)t.tv_sec, (ulong)t.tv_nsec);

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

static int _pollwait(int fd, int isread) {
    struct pollfd p;
    memset(&p, 0, sizeof(struct pollfd));
    p.fd = fd;
    p.events = isread ? POLLIN : POLLOUT;

    MYLOG("waiting for io with poll()");
    int result = poll(&p, 1, -1);
    MYLOG("poll() returned %i", result);

    if(result < 0) {
        MYLOG("error in poll(), error was: %s", strerror(errno));
        return -1;
    } else if(result == 0) {
        MYLOG("poll() called with infinite timeout, but returned no events");
        return -1;
    } else {
        /* event is ready */
        return 0;
    }
}

static int _epollwait(int fd, int isread) {
    int efd = epoll_create(1);
    MYLOG("epoll_create() returned %i", efd);
    if(efd < 0) {
        MYLOG("error in epoll_create(), error was: %s", strerror(errno));
        return -1;
    }

    struct epoll_event e;
    memset(&e, 0, sizeof(struct epoll_event));
    e.events = isread ? EPOLLIN : EPOLLOUT;

    int result = epoll_ctl(efd, EPOLL_CTL_ADD, fd, &e);
    MYLOG("epoll_ctl() op=EPOLL_CTL_ADD returned %i", result);
    if(result < 0) {
        MYLOG("error in epoll_ctl() op=EPOLL_CTL_ADD, error was: %s", strerror(errno));
        return -1;
    }

    memset(&e, 0, sizeof(struct epoll_event));

    MYLOG("waiting for io with epoll()");
    result = epoll_wait(efd, &e, 1, -1);
    MYLOG("epoll_wait() returned %i", result);

    if(result < 0) {
        MYLOG("error in epoll_wait(), error was: %s", strerror(errno));
        return -1;
    } else if(result == 0) {
        MYLOG("epoll_wait() called with infinite timeout, but returned no events");
        return -1;
    }

    result = epoll_ctl(efd, EPOLL_CTL_DEL, fd, NULL);
    MYLOG("epoll_ctl() op=EPOLL_CTL_DEL returned %i", result);
    if(result < 0) {
        MYLOG("error in epoll_ctl() op=EPOLL_CTL_DEL, error was: %s", strerror(errno));
        return -1;
    }

    close(efd);
    return 0;
}

static int _selectwait(int fd, int isread) {
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);

    MYLOG("waiting for io with select()");
    int result = select(1, isread ? &s : NULL, isread ? NULL : &s, NULL, NULL);
    MYLOG("select() returned %i", result);

    if(result < 0) {
        MYLOG("error in select(), error was: %s", strerror(errno));
        return -1;
    } else if(result == 0) {
        MYLOG("select() called with infinite timeout, but returned no events");
        return -1;
    } else {
        /* event is ready */
        return 0;
    }
}

static int _run_client(iowait_func iowait, const char* servername) {
    /* attempt to get the correct server ip address */
    struct addrinfo* serverInfo;
    int result = getaddrinfo(servername, NULL, NULL, &serverInfo);
    MYLOG("getaddrinfo() returned %i", result);
    if (result < 0) {
        MYLOG("getaddrinfo() error was: %s", strerror(errno));
        return -1;
    }

    in_addr_t serverip = ((struct sockaddr_in*) (serverInfo->ai_addr))->sin_addr.s_addr;
    freeaddrinfo(serverInfo);

    /* create a blocking socket and get a socket descriptor */
    int sd = socket(AF_INET, iowait ? (SOCK_STREAM|SOCK_NONBLOCK) : SOCK_STREAM, 0);
    MYLOG("socket() returned %i", sd);
    if (sd < 0) {
        MYLOG("socket() error was: %s", strerror(errno));
        return -1;
    }

    /* setup the socket address for connecting to the server */
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = serverip;
    serverAddr.sin_port = htons(SERVER_PORT);

    /* connect to server, blocking until the connection is ready */
    while(1) {
        result = connect(sd, (struct sockaddr *) &serverAddr, sizeof(serverAddr));
        MYLOG("connect() returned %i", result);
        if (result < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(sd, 1) < 0) {
                MYLOG("error waiting for connect(), error was: %s", strerror(errno));
                return -1;
            }
        } else if(result < 0) {
            MYLOG("connect() error was: %s", strerror(errno));
            return -1;
        } else {
            break;
        }
    }

    /* now prepare a message */
    char outbuf[BUFFERSIZE];
    memset(outbuf, 0, BUFFERSIZE);
    _fillcharbuf(outbuf, BUFFERSIZE);
    int offset = 0, amount = 0;

    /* send the bytes to the server */
    while((amount = BUFFERSIZE - offset) > 0) {
        MYLOG("trying to send %i more bytes", amount);
        ssize_t n = send(sd, &outbuf[offset], (size_t)amount, 0);
        MYLOG("send() returned %li", (long)n);

        if(n < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(sd, 0) < 0) {
                MYLOG("error waiting for send(), error was: %s", strerror(errno));
                return -1;
            }
        } else if(n < 0) {
            MYLOG("send() error was: %s", strerror(errno));
            return -1;
        } else if(n > 0) {
            MYLOG("sent %li more bytes", (long)n);
            offset += (int)n;
        } else {
            /* n == 0, on a blocking socket... */
            MYLOG("unable to send to server socket %i, and send didn't block for us", sd);
            break;
        }
    }

    MYLOG("expected %i bytes and sent %i bytes to server", BUFFERSIZE, offset);
    if(offset < BUFFERSIZE) {
        MYLOG("we did not send the expected number of bytes (%i)", BUFFERSIZE);
        return -1;
    }

    /* get ready to recv the response */
    char inbuf[BUFFERSIZE];
    memset(inbuf, 0, BUFFERSIZE);
    offset = 0;
    while((amount = BUFFERSIZE - offset) > 0) {
        MYLOG("expecting %i more bytes, waiting for data", amount);
        ssize_t n = recv(sd, &inbuf[offset], (size_t)amount, 0);
        MYLOG("recv() returned %li", (long)n);

        if(n < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(sd, 1) < 0) {
                MYLOG("error waiting for recv(), error was: %s", strerror(errno));
                return -1;
            }
        } else if(n < 0) {
            MYLOG("recv() error was: %s", strerror(errno));
            return -1;
        } else if(n > 0) {
            MYLOG("got %li more bytes", (long)n);
            offset += (int)n;
        } else {
            /* n == 0, we read EOF */
            MYLOG("read EOF, server socket %i closed", sd);
            break;
        }
    }

    MYLOG("expected %i bytes and received %i bytes from server", BUFFERSIZE, offset);
    if(offset < BUFFERSIZE) {
        MYLOG("we did not receive the expected number of bytes (%i)", BUFFERSIZE);
        return -1;
    }

    /* check that the buffers match */
    if(memcmp(outbuf, inbuf, BUFFERSIZE)) {
        MYLOG("inconsistent message - we did not receive the same bytes that we sent");
        return -1;
    } else {
        /* success from our end */
        MYLOG("consistent message - we received the same bytes that we sent");
        close(sd);
        return 0;
    }
}

static int _run_server(iowait_func iowait) {
    /* create a blocking socket and get a socket descriptor */
    int sd = socket(AF_INET, iowait ? (SOCK_STREAM|SOCK_NONBLOCK) : SOCK_STREAM, 0);
    MYLOG("socket() returned %i", sd);
    if (sd < 0) {
        MYLOG("socket() error was: %s", strerror(errno));
        return -1;
    }

    /* setup the socket address info, client has outgoing connection to server */
    struct sockaddr_in bindAddr;
    memset(&bindAddr, 0, sizeof(bindAddr));
    bindAddr.sin_family = AF_INET;
    bindAddr.sin_addr.s_addr = htons(INADDR_ANY);
    bindAddr.sin_port = htons(SERVER_PORT);

    /* bind the socket to the server port */
    int result = bind(sd, (struct sockaddr *) &bindAddr, sizeof(bindAddr));
    MYLOG("bind() returned %i", result);
    if (result < 0) {
        MYLOG("bind() error was: %s", strerror(errno));
        return -1;
    }

    /* set as server socket that will listen for clients */
    result = listen(sd, 100);
    MYLOG("listen() returned %i", result);
    if (result == -1) {
        MYLOG("listen() error was: %s", strerror(errno));
        return -1;
    }

    /* wait for an incoming connection */
    while(1) {
        result = accept(sd, NULL, NULL);
        MYLOG("accept() returned %i", result);
        if (result < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(sd, 1) < 0) {
                MYLOG("error waiting for accept(), error was: %s", strerror(errno));
                return -1;
            }
        } else if(result < 0) {
            MYLOG("accept() error was: %s", strerror(errno));
            return -1;
        } else {
            break;
        }
    }

    /* got one, now read the entire message */
    char buf[BUFFERSIZE];
    memset(buf, 0, BUFFERSIZE);
    int offset = 0, amount = 0;
    int clientsd = result;

    while((amount = BUFFERSIZE - offset) > 0) {
        MYLOG("expecting %i more bytes, waiting for data", amount);
        ssize_t n = recv(clientsd, &buf[offset], (size_t)amount, 0);
        MYLOG("recv() returned %li", (long)n);

        if(n < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(clientsd, 1) < 0) {
                MYLOG("error waiting for recv(), error was: %s", strerror(errno));
                return -1;
            }
        } else if(n < 0) {
            MYLOG("recv() error was: %s", strerror(errno));
            return -1;
        } else if(n > 0) {
            MYLOG("got %li more bytes", (long)n);
            offset += (int)n;
        } else {
            /* n == 0, we read EOF */
            MYLOG("read EOF, client socket %i closed", clientsd);
            break;
        }
    }

    MYLOG("expected %i bytes and received %i bytes from client", BUFFERSIZE, offset);
    if(offset < BUFFERSIZE) {
        MYLOG("we did not receive the expected number of bytes (%i)", BUFFERSIZE);
        return -1;
    }

    /* now send the bytes back */
    offset = 0;
    while((amount = BUFFERSIZE - offset) > 0) {
        MYLOG("trying to send %i more bytes", amount);
        ssize_t n = send(clientsd, &buf[offset], (size_t)amount, 0);
        MYLOG("send() returned %li", (long)n);

        if(n < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(clientsd, 0) < 0) {
                MYLOG("error waiting for send(), error was: %s", strerror(errno));
                return -1;
            }
        } else if(n < 0) {
            MYLOG("send() error was: %s", strerror(errno));
            return -1;
        } else if(n > 0) {
            MYLOG("sent %li more bytes", (long)n);
            offset += (int)n;
        } else {
            /* n == 0, on a blocking socket... */
            MYLOG("unable to send to client socket %i, and send didn't block for us", clientsd);
            break;
        }
    }

    MYLOG("expected %i bytes and sent %i bytes to client", BUFFERSIZE, offset);
    if(offset < BUFFERSIZE) {
        MYLOG("we did not send the expected number of bytes (%i)", BUFFERSIZE);
        return -1;
    } else {
        /* success from our end */
        close(sd);
        close(clientsd);
        return 0;
    }
}

static int _run_loopback(iowait_func iowait) {
    return 1;
}

int main(int argc, char *argv[]) {
    MYLOG("program started; %s", USAGE);

    if(argc < 3) {
        MYLOG("error, iomode and type not specified in args; see usage");
        return -1;
    }

    iowait_func wait = NULL;

    if(strncasecmp(argv[1], "blocking", 8) == 0) {
        wait = NULL;
    } else if(strncasecmp(argv[1], "nonblocking-poll", 16) == 0) {
        wait = _pollwait;
    } else if(strncasecmp(argv[1], "nonblocking-epoll", 17) == 0) {
        wait = _epollwait;
    } else if(strncasecmp(argv[1], "nonblocking-select", 18) == 0) {
        wait = _selectwait;
    } else {
        MYLOG("error, invalid iomode specified; see usage");
        return -1;
    }

    if(strncasecmp(argv[2], "client", 5) == 0) {
        if(argc < 4) {
            MYLOG("error, client mode also needs a server ip address; see usage");
            return -1;
        }
        return _run_client(wait, argv[3]);
    } else if(strncasecmp(argv[2], "server", 6) == 0) {
        return _run_server(wait);
    } else if(strncasecmp(argv[2], "loopback", 8) == 0) {
        return _run_loopback(wait);
    } else {
        MYLOG("error, invalid type specified; see usage");
        return -1;
    }
}
