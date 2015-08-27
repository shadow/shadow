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

#define USAGE "USAGE: 'shd-test-tcp iomode type'; iomode=('blocking'|'nonblocking-poll'|'nonblocking-epoll'|'nonblocking-select') type=('client' server_ip|'server')"
#define MYLOG(...) _mylog(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define SERVER_PORT 58333
#define BUFFERSIZE 20000

int tempa = 0;
int tempb = 1;
typedef enum _waittype {
    WAIT_WRITE, WAIT_READ
} waittype;
typedef int (*iowait_func)(int fd, waittype t);

static void _mylog(const char* fileName, const int lineNum, const char* funcName, const char* format, ...) {
    struct timeval t;
    memset(&t, 0, sizeof(struct timeval));
    gettimeofday(&t, NULL);
    fprintf(stdout, "[%ld.%.06ld] [%s:%i] [%s] ", (long)t.tv_sec, (long)t.tv_usec, fileName, lineNum, funcName);

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

static int _wait_poll(int fd, waittype t) {
    struct pollfd p;
    memset(&p, 0, sizeof(struct pollfd));
    p.fd = fd;
    p.events = (t == WAIT_READ) ? POLLIN : POLLOUT;

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

static int _wait_epoll(int fd, waittype t) {
    int efd = epoll_create(1);
    MYLOG("epoll_create() returned %i", efd);
    if(efd < 0) {
        MYLOG("error in epoll_create(), error was: %s", strerror(errno));
        return -1;
    }

    struct epoll_event e;
    memset(&e, 0, sizeof(struct epoll_event));
    e.events = (t == WAIT_READ) ? EPOLLIN : EPOLLOUT;

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

static int _wait_select(int fd, waittype t) {
    fd_set s;
    FD_ZERO(&s);
    FD_SET(fd, &s);

    MYLOG("waiting for io with select()");
    int result = select(fd+1, (t == WAIT_READ) ? &s : NULL, (t == WAIT_WRITE) ? &s : NULL, NULL, NULL);
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

static int _do_addr(const char* name, struct sockaddr_in* addrout) {
    memset(addrout, 0, sizeof(struct sockaddr_in));
    addrout->sin_family = AF_INET;
    addrout->sin_addr.s_addr = htons(INADDR_ANY);
    addrout->sin_port = htons(SERVER_PORT);

    if(name) {
        /* attempt to get the correct server ip address */
        struct addrinfo* info;
        int result = getaddrinfo(name, NULL, NULL, &info);
        MYLOG("getaddrinfo() returned %i", result);
        if (result < 0) {
            MYLOG("getaddrinfo() error was: %s", strerror(errno));
            return -1;
        }

        addrout->sin_addr.s_addr = ((struct sockaddr_in*) (info->ai_addr))->sin_addr.s_addr;
        freeaddrinfo(info);
    }

    return 0;
}

static int _do_socket(int type, int* fdout) {
    /* create a socket and get a socket descriptor */
    int sd = socket(AF_INET, type, 0);
    MYLOG("socket() returned %i", sd);
    if (sd < 0) {
        MYLOG("socket() error was: %s", strerror(errno));
        return -1;
    }

    int yes = 1;
    int result = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
    MYLOG("setsockopt() returned %i", result);
    if(result < 0) {
        MYLOG("setsockopt() error was: %s", strerror(errno));
        return -1;
    }

    if(fdout) {
        *fdout = sd;
    }

    return 0;
}

static int _do_connect(int fd, struct sockaddr_in* serveraddr, iowait_func iowait) {
    /* connect to server, blocking until the connection is ready */
    while(1) {
        int result = connect(fd, (struct sockaddr *) serveraddr, sizeof(struct sockaddr_in));
        MYLOG("connect() returned %i", result);
        if (result < 0 && iowait && errno == EINPROGRESS) {
            if(iowait(fd, WAIT_WRITE) < 0) {
                MYLOG("error waiting for connect()");
                return -1;
            }
            continue;
        } else if(result < 0) {
            MYLOG("connect() error was: %s", strerror(errno));
            return -1;
        } else {
            break;
        }
    }
    return 0;
}

static int _do_serve(int fd, struct sockaddr_in* bindaddr, iowait_func iowait, int* clientsdout) {
    /* bind the socket to the server port */
    int result = bind(fd, (struct sockaddr *) bindaddr, sizeof(struct sockaddr_in));
    MYLOG("bind() returned %i", result);
    if (result < 0) {
        MYLOG("bind() error was: %s", strerror(errno));
        return -1;
    }

    /* set as server socket that will listen for clients */
    result = listen(fd, 100);
    MYLOG("listen() returned %i", result);
    if (result == -1) {
        MYLOG("listen() error was: %s", strerror(errno));
        return -1;
    }

    /* wait for an incoming connection */
    while(1) {
        result = accept(fd, NULL, NULL);
        MYLOG("accept() returned %i", result);
        if (result < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(fd, WAIT_READ) < 0) {
                MYLOG("error waiting for accept()");
                return -1;
            }
        } else if(result < 0) {
            MYLOG("accept() error was: %s", strerror(errno));
            return -1;
        } else {
            break;
        }
    }

    if(clientsdout) {
        *clientsdout = result;
    }
    return 0;
}

static int _do_send(int fd, char* buf, iowait_func iowait) {
    int offset = 0, amount = 0;

    /* send the bytes to the server */
    while((amount = BUFFERSIZE - offset) > 0) {
        MYLOG("trying to send %i more bytes", amount);
        ssize_t n = send(fd, &buf[offset], (size_t)amount, 0);
        MYLOG("send() returned %li", (long)n);

        if(n < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(fd, WAIT_WRITE) < 0) {
                MYLOG("error waiting for send()");
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
            MYLOG("unable to send to server socket %i, and send didn't block for us", fd);
            break;
        }
    }

    MYLOG("sent %i/%i bytes %s", offset, BUFFERSIZE, (offset == BUFFERSIZE) ? ":)" : ":(");
    if(offset < BUFFERSIZE) {
        MYLOG("we did not send the expected number of bytes (%i)!", BUFFERSIZE);
        return -1;
    }

    return 0;
}

static int _do_recv(int fd, char* buf, iowait_func iowait) {
    int offset = 0, amount = 0;

    while((amount = BUFFERSIZE - offset) > 0) {
        MYLOG("expecting %i more bytes, waiting for data", amount);
        ssize_t n = recv(fd, &buf[offset], (size_t)amount, 0);
        MYLOG("recv() returned %li", (long)n);

        if(n < 0 && iowait && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if(iowait(fd, WAIT_READ) < 0) {
                MYLOG("error waiting for recv()");
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
            MYLOG("read EOF, server socket %i closed", fd);
            break;
        }
    }

    MYLOG("received %i/%i bytes %s", offset, BUFFERSIZE, (offset == BUFFERSIZE) ? ":)" : ":(");
    if(offset < BUFFERSIZE) {
        MYLOG("we did not receive the expected number of bytes (%i)!", BUFFERSIZE);
        return -1;
    }

    return 0;
}

static int _run_client(iowait_func iowait, const char* servername) {
    struct sockaddr_in serveraddr;
    if(_do_addr(servername, &serveraddr) < 0) {
        return -1;
    }

    int serversd;
    int type = iowait ? (SOCK_STREAM|SOCK_NONBLOCK) : SOCK_STREAM;
    if(_do_socket(type, &serversd) < 0) {
        return -1;
    }

    if(_do_connect(serversd, &serveraddr, iowait) < 0) {
        return -1;
    }

    /* now prepare a message */
    char outbuf[BUFFERSIZE];
    memset(outbuf, 0, BUFFERSIZE);
    _fillcharbuf(outbuf, BUFFERSIZE);

    /* send to server */
    if(_do_send(serversd, outbuf, iowait) < 0) {
        return -1;
    }

    /* get ready to recv the response */
    char inbuf[BUFFERSIZE];
    memset(inbuf, 0, BUFFERSIZE);

    /* recv from server */
    if(_do_recv(serversd, inbuf, iowait) < 0) {
        return -1;
    }

    close(serversd);

    /* check that the buffers match */
    if(memcmp(outbuf, inbuf, BUFFERSIZE)) {
        MYLOG("inconsistent message - we did not receive the same bytes that we sent :(");
        return -1;
    } else {
        /* success from our end */
        MYLOG("consistent message - we received the same bytes that we sent :)");
        return 0;
    }
}

static int _run_server(iowait_func iowait) {
    int listensd;
    int type = iowait ? (SOCK_STREAM|SOCK_NONBLOCK) : SOCK_STREAM;
    if(_do_socket(type, &listensd) < 0) {
        return -1;
    }

    /* setup the socket address info, client has outgoing connection to server */
    struct sockaddr_in bindaddr;
    if(_do_addr(NULL, &bindaddr) < 0) {
        return -1;
    }

    int clientsd;
    if(_do_serve(listensd, &bindaddr, iowait, &clientsd) < 0) {
        return -1;
    }

    /* got one, now read the entire message */
    char buf[BUFFERSIZE];
    memset(buf, 0, BUFFERSIZE);

    if(_do_recv(clientsd, buf, iowait) < 0) {
        return -1;
    }

    if(_do_send(clientsd, buf, iowait) < 0) {
        return -1;
    }

    /* success from our end */
    close(clientsd);
    close(listensd);
    return 0;
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
        wait = _wait_poll;
    } else if(strncasecmp(argv[1], "nonblocking-epoll", 17) == 0) {
        wait = _wait_epoll;
    } else if(strncasecmp(argv[1], "nonblocking-select", 18) == 0) {
        wait = _wait_select;
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
    } else {
        MYLOG("error, invalid type specified; see usage");
        return -1;
    }
}
