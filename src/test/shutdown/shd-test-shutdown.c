#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <linux/sockios.h>
#include <sys/ioctl.h>

#include <../shd-test-common.h>

static int _test_shutdown_tcp(int call_connect, int shut_client, int how) {
    int sd = 0, cd = 0, sd_child = 0;
    in_port_t server_port = 0;
    int retval = EXIT_FAILURE;

    if(common_setup_tcp_sockets(&sd, &cd, &server_port) < 0) {
        goto fail;
    }

    if(call_connect) {
        if(common_connect_tcp_sockets(sd, cd, &sd_child, server_port) < 0) {
            goto fail;
        }
    }

    int thedesc = shut_client ? cd : sd_child;

    int result = shutdown(thedesc, how);
    retval = errno;

    printf("shutdown() returned %i\n", result);

    if(result < 0) {
        printf("shutdown() error was: %s\n", strerror(errno));
    } else {
        retval = 0;
    }

fail:
    close(cd);
    close(sd_child);
    close(sd);
    return retval;
}

static int _test_tcp_shutdown_before_connect(){
    printf("########## running _test_tcp_shutdown_before_connect\n");

    int result = 0;

    result = _test_shutdown_tcp(0, 1, SHUT_RDWR);
    if(result != ENOTCONN) {
        printf("Expecting shutdown(SHUT_RDWR) on unconnected socket to return ENOTCONN instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(0, 1, SHUT_RD);
    if(result != ENOTCONN) {
        printf("Expecting shutdown(SHUT_RD) on unconnected socket to return ENOTCONN instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(0, 1, SHUT_WR);
    if(result != ENOTCONN) {
        printf("Expecting shutdown(SHUT_WR) on unconnected socket to return ENOTCONN instead of %i\n", result);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _test_tcp_shutdown_after_connect(){
    printf("########## running _test_tcp_shutdown_after_connect\n");

    int result = 0;

    result = _test_shutdown_tcp(1, 1, SHUT_RDWR);
    if(result != 0) {
        printf("Expecting shutdown(SHUT_RDWR) on client socket to return 0 instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(1, 1, SHUT_RD);
    if(result != 0) {
        printf("Expecting shutdown(SHUT_RD) on client socket to return 0 instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(1, 1, SHUT_WR);
    if(result != 0) {
        printf("Expecting shutdown(SHUT_WR) on client socket to return 0 instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(1, 0, SHUT_RDWR);
    if(result != 0) {
        printf("Expecting shutdown(SHUT_RDWR) on server socket to return 0 instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(1, 0, SHUT_RD);
    if(result != 0) {
        printf("Expecting shutdown(SHUT_RD) on server socket to return 0 instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(1, 0, SHUT_WR);
    if(result != 0) {
        printf("Expecting shutdown(SHUT_WR) on server socket to return 0 instead of %i\n", result);
        return EXIT_FAILURE;
    }

    result = _test_shutdown_tcp(1, 1, 666);
    if(result != -1 && errno != EINVAL) {
        printf("Expecting shutdown(SHUT_WR) on server socket to return -1(EINVAL) instead of %i\n", result);
        return EXIT_FAILURE;
    }

    if(shutdown(66666, SHUT_RDWR) != -1 && errno != ENOTCONN) {
        printf("Expecting shutdown(SHUT_RDWR) on non-socket to return -1(EBADF) instead of %i\n", result);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int _test_read_after_shutdown() {
    printf("########## running _test_read_after_shutdown\n");

    int sd = 0, cd = 0, sd_child = 0;

    int result = common_get_connected_tcp_sockets(&sd, &sd_child, &cd);
    if(result != EXIT_SUCCESS) {
        printf("Unable to get connected tcp sockets\n");
        goto fail;
    }

    char buf[4096];
    memset(buf, 0, 4096);

    ssize_t bytes = send(cd, buf, 4096, 0);
    printf("send() returned %li bytes before SHUT_RD\n", (long int)bytes);

    if(bytes != 4096) {
        printf("Unable to send 4096 bytes\n");
        goto fail;
    }

    usleep(10000); // 10 millis

    result = shutdown(sd_child, SHUT_RD);
    printf("shutdown(SHUT_RD) returned %i\n", result);

    if(result != 0) {
        printf("Unable to shutdown socket\n");
        goto fail;
    }

    bytes = recv(sd_child, buf, 4096, 0);
    printf("1st recv() returned %li bytes after SHUT_RD\n", (long int)bytes);

    if(bytes != 4096) {
        printf("after shutdown(SHUT_RD), recv should still allow us to read the existing 4096 bytes\n");
        goto fail;
    }

    bytes = recv(sd_child, buf, 4096, 0);
    printf("2nd recv() returned %li bytes after SHUT_RD\n", (long int)bytes);

    if(bytes != 0) {
        printf("after shutdown(SHUT_RD) and recving the existing 4096 bytes, we should read EOF (0)\n");
        goto fail;
    }

    bytes = send(cd, buf, 4096, 0);
    printf("1st send() returned %li bytes after SHUT_RD\n", (long int)bytes);

    if(bytes != 4096) {
        printf("after shutdown(SHUT_RD), the peer should still be able to send\n");
        goto fail;
    }

    bytes = send(sd_child, buf, 4096, 0);
    printf("2nd send() returned %li bytes after SHUT_RD%s\n", (long int)bytes, bytes == -1 ? strerror(errno) : "");

    if(bytes != 4096) {
        printf("after shutdown(SHUT_RD), we should still be able to send\n");
        goto fail;
    }

    usleep(10000); // 10 millis

    bytes = recv(cd, buf, 4096, 0);
    printf("3rd recv() returned %li bytes after SHUT_RD\n", (long int)bytes);

    if(bytes != 4096) {
        printf("after shutdown(SHUT_RD), peer should read what we sent\n");
        goto fail;
    }

    bytes = recv(sd_child, buf, 4096, 0);
    printf("4th recv() returned %li bytes after SHUT_RD\n", (long int)bytes);

    /* i expected this to return 0, but it in fact returned 4096 in cent os 7.
     * it appears as if SHUT_RD only causes the recv() to return 0 when there
     * is no data currently available rather than -1 EAGAIN, but when new data
     * arrives it can be read again.
     */
//    if(bytes != 0) {
//        printf("after shutdown(SHUT_RD) and read EOF (0), we should read EOF again\n");
//        return EXIT_FAILURE;
//    }

    close(cd);
    close(sd_child);
    close(sd);
    return EXIT_SUCCESS;
fail:
    close(cd);
    close(sd_child);
    close(sd);
    return EXIT_FAILURE;
}

static int _test_write_after_shutdown() {
    printf("########## running _test_write_after_shutdown\n");

    int sd = 0, cd = 0, sd_child = 0;

    int result = common_get_connected_tcp_sockets(&sd, &sd_child, &cd);
    if(result != EXIT_SUCCESS) {
        printf("Unable to get connected tcp sockets\n");
        goto fail;
    }

    char buf[4096];
    memset(buf, 0, 4096);

    ssize_t bytes = send(cd, buf, 96, 0);
    printf("1st send() returned %li bytes before SHUT_WR\n", (long int)bytes);

    if(bytes != 96) {
        printf("Unable to send 96 bytes\n");
        goto fail;
    }

    usleep(10000); // 10 millis

    result = shutdown(cd, SHUT_WR);
    printf("shutdown(SHUT_WR) returned %i\n", result);

    if(result != 0) {
        printf("Unable to shutdown socket\n");
        goto fail;
    }

    bytes = send(cd, buf, 4000, 0);
    printf("2nd send() returned %li bytes after SHUT_WR, errno is %i: %s\n", (long int)bytes, errno, strerror(errno));

    if(bytes != -1 || errno != EPIPE) {
        printf("after shutdown(SHUT_WR), send should not allow us to send more and should set EPIPE errno\n");
        goto fail;
    }

    bytes = recv(sd_child, buf, 4096, 0);
    printf("1st recv() returned %li bytes after SHUT_WR\n", (long int)bytes);

    if(bytes != 96) {
        printf("after shutdown(SHUT_WR) peer should be able to read the 96 bytes we sent\n");
        goto fail;
    }

    bytes = recv(sd_child, buf, 4096, 0);
    printf("2nd recv() returned %li bytes after SHUT_WR\n", (long int)bytes);

    if(bytes != 0) {
        printf("after shutdown(SHUT_WR) peer should read EOF (0) when socket is empty\n");
        goto fail;
    }

    bytes = send(sd_child, buf, 4096, 0);
    printf("3rd send() returned %li bytes after SHUT_WR\n", (long int)bytes);

    if(bytes != 4096) {
        printf("after shutdown(SHUT_WR), the peer should still be able to send\n");
        goto fail;
    }

    usleep(10000); // 10 millis

    bytes = recv(cd, buf, 4096, 0);
    printf("3rd recv() returned %li bytes after SHUT_WR\n", (long int)bytes);

    if(bytes != 4096) {
        printf("after shutdown(SHUT_WR), we should be able to read what peer sent\n");
        goto fail;
    }

    close(cd);
    close(sd_child);
    close(sd);
    return EXIT_SUCCESS;
fail:
    close(cd);
    close(sd_child);
    close(sd);
    return EXIT_FAILURE;
}

static int _test_write_blocked_shutdown() {
    /* if you write a bunch to a socket and then shutdown **after the other end already shutdown**,
     * the fin should not be sent until after the buffer is cleared. */

    printf("########## running _test_write_blocked_shutdown\n");

    int sd = 0, cd = 0, sd_child = 0;

    int result = common_get_connected_tcp_sockets(&sd, &sd_child, &cd);
    if(result != EXIT_SUCCESS) {
        printf("Unable to get connected tcp sockets\n");
        goto fail;
    }

    result = shutdown(sd_child, SHUT_WR);
    printf("shutdown(SHUT_WR) on server child returned %i\n", result);

    char buf[60000];
    memset(buf, 0, 60000);

    ssize_t bytes = send(cd, buf, 60000, 0);

    result = shutdown(cd, SHUT_WR);
    printf("shutdown(SHUT_WR) on client returned %i\n", result);

    if(result != 0) {
        printf("Unable to shutdown socket\n");
        goto fail;
    }

    /* wait for the data to get to the receiver */
    usleep(10000); // 10 millis

    size_t totalBytes = 0;
    while(1) {
        bytes = recv(sd_child, buf, 4096, 0);

        if(bytes > 0) {
            totalBytes += (size_t)bytes;
            printf("recv() got %li more bytes, total is %li\n", (long int)bytes, (long int) totalBytes);
        } else if(bytes == -1 && errno == EWOULDBLOCK) {
            printf("recv() would block, pausing for 1 millisecond\n");
            usleep(1000); // 1 milli
        } else if(bytes == 0) {
            printf("recv() returned EOF\n");
            break;
        } else {
            printf("recv() returned error %i: %s\n", errno, strerror(errno));
            break;
        }
    }

    printf("recv() %li total bytes after SHUT_WR\n", (long int)totalBytes);

    if(totalBytes != 60000) {
        printf("after shutdown(SHUT_WR) peer should be able to read the 60000 bytes we sent\n");
        goto fail;
    }

    close(cd);
    close(sd_child);
    close(sd);
    return EXIT_SUCCESS;
fail:
    close(cd);
    close(sd_child);
    close(sd);
    return EXIT_FAILURE;
}

static int _test_udp_shutdown() {
    printf("########## running _test_udp_shutdown\n");

    int udpsock = socket(AF_INET, SOCK_DGRAM, 0);
    int result = 0;
    if((result = shutdown(udpsock, SHUT_RDWR)) != -1 || errno != ENOTCONN) {
        printf("Expected shutdown(SHUT_RDWR) on udp socket to return -1 (ENOTCONN) instead of %i\n", result);
        printf("shutdown() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    close(udpsock);

    udpsock = socket(AF_INET, SOCK_DGRAM, 0);
    if((result = shutdown(udpsock, SHUT_RD)) != -1 || errno != ENOTCONN) {
        printf("Expected shutdown(SHUT_RD) on udp socket to return -1 (ENOTCONN) instead of %i\n", result);
        printf("shutdown() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    close(udpsock);

    udpsock = socket(AF_INET, SOCK_DGRAM, 0);
    if((result = shutdown(udpsock, SHUT_WR)) != -1 || errno != ENOTCONN) {
        printf("Expected shutdown(SHUT_WR) on udp socket to return -1 (ENOTCONN) instead of %i\n", result);
        printf("shutdown() error was: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    close(udpsock);

    return EXIT_SUCCESS;
}

int run() {
    /* ignore broken pipes signals so we do not crash our process */
    signal(SIGPIPE, SIG_IGN);

    if(_test_tcp_shutdown_before_connect() == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if(_test_tcp_shutdown_after_connect() == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if(_test_read_after_shutdown() == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if(_test_write_after_shutdown() == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if(_test_write_blocked_shutdown() == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    if(_test_udp_shutdown() == EXIT_FAILURE) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main() {
    printf("########## shutdown test starting ##########\n");

    if(run() == EXIT_SUCCESS) {
        printf("########## shutdown test passed ##########\n");
        return EXIT_SUCCESS;
    } else {
        printf("########## shutdown test failed ##########\n");
        return EXIT_FAILURE;
    }
}
