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
#include <fcntl.h>

#define USAGE "USAGE: 'shd-test-tcp iomode type'; iomode=('blocking'|'nonblocking-poll'|'nonblocking-epoll'|'nonblocking-select') type=('client' server_ip|'server')"
#define MYLOG(...) _mylog(__FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define SERVER_PORT 58333
#define BUFFERSIZE 20000
#define ARRAY_LENGTH(arr)  (sizeof (arr) / sizeof ((arr)[0]))

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

/* make the socket blocking. Returns 0 on success, or -1 on error */
static int _make_socket_blocking(int fd)
{
   int flags = fcntl(fd, F_GETFL, 0);
   if (flags < 0) return -1;
   flags = flags&~O_NONBLOCK;
   return fcntl(fd, F_SETFL, flags);
}

static int _test_iov_client(int serverfd)
{
#undef LOG_ERROR_AND_RETURN
#define LOG_ERROR_AND_RETURN(fmt, ...)                                  \
    do {                                                                \
        MYLOG("error: " fmt, ##__VA_ARGS__);                            \
        return -1;                                                      \
    } while (0)

    struct iovec iov[UIO_MAXIOV];

    /**** we want a blocking socket ****/
    if (_make_socket_blocking(serverfd)) {
        LOG_ERROR_AND_RETURN("cannot make socket blocking");
    }

    int rv = 0;
    int expected_errno = 0;
    int expected_rv = 0;

    const struct {
        const char* funcname;
        ssize_t (*funcptr)(int, const struct iovec *, int);
    } funcs[] = {
        {"readv", readv},
        {"writev", writev}
    };
    for (int i = 0; i < ARRAY_LENGTH(funcs); ++i) {
        const char* funcname = funcs[i].funcname;

        rv = funcs[i].funcptr(serverfd, iov, -1);
        expected_errno = EINVAL;
        if ((rv != -1) || (errno != expected_errno)) {
            LOG_ERROR_AND_RETURN("%s() should fail on an invalid arg. "
                                 "expected rv: -1, actual: %d; "
                                 "expected errno: %d, actual: %d",
                                 funcname, rv, expected_errno, errno);
        }

        rv = funcs[i].funcptr(serverfd, iov, UIO_MAXIOV+1);
        expected_errno = EINVAL;
        if ((rv != -1) || (errno != expected_errno)) {
            LOG_ERROR_AND_RETURN("%s() should fail on an invalid arg. "
                                 "expected rv: -1, actual: %d; "
                                 "expected errno: %d, actual: %d",
                                 funcname, rv, expected_errno, errno);
        }

        rv = funcs[i].funcptr(1923, iov, UIO_MAXIOV+1);
        expected_errno = EBADF;
        if ((rv != -1) || (errno != expected_errno)) {
            LOG_ERROR_AND_RETURN("%s() should fail on an invalid fd. "
                                 "expected rv: -1, actual: %d; "
                                 "expected errno: %d, actual: %d",
                                 funcname, rv, expected_errno, errno);
        }
    }

    rv = readv(serverfd, iov, 0);
    if (rv == -1) {
        LOG_ERROR_AND_RETURN("should not fail when passing '0' as the iovcnt");
    }

    // make all bases point to a string but all len to 0
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = "REAL DATA";
        iov[i].iov_len = 0;
    }

    // should write 0 bytes
    rv = writev(serverfd, iov, ARRAY_LENGTH(iov));
    expected_rv = 0;
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

    // write two real blocks
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = "REAL DATA";
        iov[i].iov_len = 0;
    }

    const char block_1_data[] = "hellloo o 12  o .<  oadsa flasll llallal";
    iov[31].iov_base = (void*)block_1_data;
    iov[31].iov_len = strlen(block_1_data);

    const char block_2_data[] = "___ = ==xll3kjf l  llxkf 0487oqlkj kjalskkkf";
    iov[972].iov_base = (void*)block_2_data;
    iov[972].iov_len = strlen(block_2_data);

    rv = writev(serverfd, iov, ARRAY_LENGTH(iov));
    expected_rv = strlen(block_1_data) + strlen(block_2_data);
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

    MYLOG("wrote two blocks. now wait for sever's OK... ");

    /* read to sync with server */
    char syncbuf[2] = {0};
    rv = read(serverfd, syncbuf, sizeof syncbuf);
    MYLOG("got rv= %d", rv);
    if (rv != sizeof (syncbuf) || memcmp(syncbuf, "OK", sizeof syncbuf)) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %zu, actual: %d", sizeof syncbuf, rv);
    }

    // write one real block
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = "REAL DATA";
        iov[i].iov_len = 0;
    }

    const char block_3_data[] = "make america great again! ;)";
    iov[1023].iov_base = (void*)block_3_data;
    iov[1023].iov_len = strlen(block_3_data);

    rv = writev(serverfd, iov, ARRAY_LENGTH(iov));
    expected_rv = strlen(block_3_data);
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d", expected_rv, rv);
    }

    MYLOG("wrote one block. now wait for sever's OK... ");
    /* read to sync with server */
    memset(syncbuf, 0, sizeof syncbuf);
    rv = read(serverfd, syncbuf, sizeof syncbuf);
    MYLOG("got rv= %d", rv);
    if (rv != sizeof (syncbuf) || memcmp(syncbuf, "OK", sizeof syncbuf)) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %zu, actual: %d", sizeof syncbuf, rv);
    }

    MYLOG("all good");

#undef LOG_ERROR_AND_RETURN
    return 0;
}

static int _test_iov_server(int clientfd)
{
#undef LOG_ERROR_AND_RETURN
#define LOG_ERROR_AND_RETURN(fmt, ...)                                  \
    do {                                                                \
        MYLOG("error: " fmt, ##__VA_ARGS__);                            \
        return -1;                                                      \
    } while (0)

    /**** we want a blocking socket ****/
    if (_make_socket_blocking(clientfd)) {
        LOG_ERROR_AND_RETURN("cannot make socket blocking");
    }

    struct iovec iov[UIO_MAXIOV];

    int rv = 0;
    int expected_rv = 0;

    /****
     **** read into one base
     ****/
    const char block_1_data[] = "hellloo o 12  o .<  oadsa flasll llallal";
    const char block_2_data[] = "___ = ==xll3kjf l  llxkf 0487oqlkj kjalskkkf";

    char sharedreadbuf[14] = {[0 ... 13] = 'y'};
    const char compare_buf[14] = {[0 ... 13] = 'y'};
    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = sharedreadbuf;
        iov[i].iov_len = 0;
    }

    // to contain data read by readv(). "- 1" to discount the
    // nul-terminator
    const size_t num_real_bytes = (sizeof block_1_data - 1) + (sizeof block_2_data - 1);
    char readbuf[num_real_bytes + 5] = {[0 ... num_real_bytes + 5 - 1] = 'z'};
    iov[1023].iov_base = readbuf;
    iov[1023].iov_len = sizeof readbuf;

    MYLOG("start readv()ing... ");
    rv = readv(clientfd, iov, ARRAY_LENGTH(iov));
    MYLOG("got rv= %d", rv);

    // verify
    expected_rv = strlen(block_1_data) + strlen(block_2_data);
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d; errno: %d",
            expected_rv, rv, errno);
    }

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        if (i == 1023) {
            // readv should not have touched the iov_len
            const size_t expected_len = sizeof readbuf;
            if (iov[i].iov_len != expected_len) {
                LOG_ERROR_AND_RETURN(
                    "readv produces wrong iov_len: %zu, expected: %zu",
                    iov[i].iov_len, expected_len);
            }
        } else {
            if (iov[i].iov_len || memcmp(iov[i].iov_base, compare_buf, sizeof compare_buf)) {
                LOG_ERROR_AND_RETURN("WHAT DID YOU DO!!!");
            }
        }
    }

    if (memcmp(readbuf, block_1_data, strlen(block_1_data))) {
        LOG_ERROR_AND_RETURN("read data has incorrect bytes");
    }
    if (memcmp(readbuf + strlen(block_1_data), block_2_data, strlen(block_2_data))) {
        LOG_ERROR_AND_RETURN("read data has incorrect bytes");
    }
    if (memcmp(readbuf + strlen(block_1_data) + strlen(block_2_data), "zzzzz", 5)) {
        LOG_ERROR_AND_RETURN("readv() touched more memory than it should have");
    }

    // send "OK" cuz client is waiting for it
    write(clientfd, "OK", 2);

    /****
     **** read into two bases
     ****/
    const char block_3_data[] = "make america great again! ;)";

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        iov[i].iov_base = sharedreadbuf;
        iov[i].iov_len = 0;
    }

    // to contain data read by readv(). "- 1" to discount the
    // nul-terminator
    const size_t num_real_bytes2 = (sizeof block_3_data - 1);
    char readbuf1[12 + 9] = {[0 ... 12 + 9 - 1] = 'M'};
    char readbuf2[(num_real_bytes2 - 12) + 5] = {[0 ... (num_real_bytes2 - 12) + 5 - 1] = 'N'};
    iov[441].iov_base = readbuf1;
    iov[441].iov_len = 12;
    iov[820].iov_base = readbuf2;
    iov[820].iov_len = 16;

    MYLOG("start readv()ing... ");
    rv = readv(clientfd, iov, ARRAY_LENGTH(iov));
    MYLOG("got rv= %d", rv);

    // verify
    expected_rv = strlen(block_3_data);
    if (rv != expected_rv) {
        LOG_ERROR_AND_RETURN(
            "expected rv: %d, actual: %d; errno: %d",
            expected_rv, rv, errno);
    }

    for (int i = 0; i < ARRAY_LENGTH(iov); ++i) {
        if (i == 441 || i == 820) {
            // readv should not have touched the iov_len
            const size_t expected_len = (i == 441) ? 12 : 16;
            if (iov[i].iov_len != expected_len) {
                LOG_ERROR_AND_RETURN(
                    "readv produces wrong iov_len: %zu, expected: %zu",
                    iov[i].iov_len, expected_len);
            }
        } else {
            if (iov[i].iov_len || memcmp(iov[i].iov_base, compare_buf, sizeof compare_buf)) {
                LOG_ERROR_AND_RETURN("WHAT DID YOU DO!!!");
            }
        }
    }

    if (memcmp(readbuf1, "make america", 12)) {
        LOG_ERROR_AND_RETURN("read data has incorrect bytes");
    }
    if (memcmp(readbuf1 + 12, "MMMMMMMMM", 9)) {
        LOG_ERROR_AND_RETURN("readv() touched more memory than it should have");
    }
    if (memcmp(readbuf2, " great again! ;)", 16)) {
        LOG_ERROR_AND_RETURN("read data has incorrect bytes");
    }
    if (memcmp(readbuf2 + 16, "NNNNN", 5)) {
        LOG_ERROR_AND_RETURN("readv() touched more memory than it should have");
    }

    // send "OK" cuz client is waiting for it
    write(clientfd, "OK", 2);

    MYLOG("all good");

#undef LOG_ERROR_AND_RETURN

    return 0;
}

static int _run_client(iowait_func iowait, const char* servername, const int use_iov) {
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

    if (!use_iov) {
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

        /* check that the buffers match */
        if(memcmp(outbuf, inbuf, BUFFERSIZE)) {
            MYLOG("inconsistent message - we did not receive the same bytes that we sent :(");
            return -1;
        } else {
            /* success from our end */
            MYLOG("consistent message - we received the same bytes that we sent :)");
        }
    }
    else {
        if (_test_iov_client(serversd) < 0) {
            return -1;
        }
    }

    close(serversd);

    return 0;
}

static int _run_server(iowait_func iowait, int use_iov) {
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

    if (!use_iov) {
        /* got one, now read the entire message */
        char buf[BUFFERSIZE];
        memset(buf, 0, BUFFERSIZE);

        if(_do_recv(clientsd, buf, iowait) < 0) {
            return -1;
        }

        if(_do_send(clientsd, buf, iowait) < 0) {
            return -1;
        }
    }
    else {
        if (_test_iov_server(clientsd) < 0) {
            return -1;
        }
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
    int use_iov = 0;

    if(strncasecmp(argv[1], "blocking", 8) == 0) {
        wait = NULL;
    } else if(strncasecmp(argv[1], "nonblocking-poll", 16) == 0) {
        wait = _wait_poll;
    } else if(strncasecmp(argv[1], "nonblocking-epoll", 17) == 0) {
        wait = _wait_epoll;
    } else if(strncasecmp(argv[1], "nonblocking-select", 18) == 0) {
        wait = _wait_select;
    } else if(strncasecmp(argv[1], "iov", 3) == 0) {
        wait = NULL;
        use_iov = 1;
    } else {
        MYLOG("error, invalid iomode specified; see usage");
        return -1;
    }

    if(strncasecmp(argv[2], "client", 5) == 0) {
        if(argc < 4) {
            MYLOG("error, client mode also needs a server ip address; see usage");
            return -1;
        }
        return _run_client(wait, argv[3], use_iov);
    } else if(strncasecmp(argv[2], "server", 6) == 0) {
        return _run_server(wait, use_iov);
    } else {
        MYLOG("error, invalid type specified; see usage");
        return -1;
    }
}
