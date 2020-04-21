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
#include <linux/sockios.h>
#include <sys/ioctl.h>

#include "test/test_common.h"

#define BUFFERSIZE 1048576

#undef SOCKTEST_IOCTL_INQ
#ifdef SIOCINQ
#define SOCKTEST_IOCTL_INQ SIOCINQ
#elif defined ( TIOCINQ )
#define SOCKTEST_IOCTL_INQ TIOCINQ
#elif defined ( FIONREAD )
#define SOCKTEST_IOCTL_INQ FIONREAD
#endif

#undef SOCKTEST_IOCTL_OUTQ
#ifdef SIOCOUTQ
#define SOCKTEST_IOCTL_OUTQ SIOCOUTQ
#elif defined ( TIOCOUTQ )
#define SOCKTEST_IOCTL_OUTQ TIOCOUTQ
#endif

static void _fillcharbuf(char* buffer, int size) {
    int i = 0;
    for (i = 0; i < size; i++) {
        int n = rand() % 26;
        buffer[i] = 'a' + n;
    }
}

static int set_sizes(int fd, unsigned int amt_snd, unsigned int amt_rcv) {
    socklen_t optionLength;

    optionLength = (socklen_t) sizeof(unsigned int);
    int result = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &amt_snd, optionLength);
    if (result < 0) {
        printf("setsockopt: failed to obtain SNDBUF for socket %d: %s\n", fd, strerror(errno));
        return -1;
    }

    optionLength = (socklen_t) sizeof(unsigned int);
    result = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &amt_rcv, optionLength);
    if (result < 0) {
        printf("setsockopt: failed to obtain RCVBUF for socket %d: %s\n", fd, strerror(errno));
        return -1;
    }

    return 0;
}

static int get_sizes(int fd, unsigned int* amt_snd, unsigned int* amt_rcv) {
    unsigned int sendSize = 0, receiveSize = 0;
    socklen_t optionLength;

    optionLength = (socklen_t) sizeof(unsigned int);
    int result = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendSize, &optionLength);
    if (result < 0) {
        printf("getsockopt: failed to obtain SNDBUF for socket %d: %s\n", fd, strerror(errno));
        return -1;
    }

    optionLength = (socklen_t) sizeof(unsigned int);
    result = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &receiveSize, &optionLength);
    if (result < 0) {
        printf("getsockopt: failed to obtain RCVBUF for socket %d: %s\n", fd, strerror(errno));
        return -1;
    }

    if(amt_snd) {
        *amt_snd = sendSize;
    }
    if(amt_rcv) {
        *amt_rcv = receiveSize;
    }

    return 0;
}

static int get_lengths(int fd, unsigned int* amt_snd, unsigned int* amt_rcv) {
    unsigned int sendLength = 0, receiveLength = 0;
    socklen_t optionLength;
    int result = 0;

#ifdef SOCKTEST_IOCTL_OUTQ
    result = ioctl(fd, SOCKTEST_IOCTL_OUTQ, &sendLength);
    if (result < 0) {
        printf("ioctl: failed to obtain OUTQLEN for socket %d: %s\n", fd, strerror(errno));
        return -1;
    }
    if(amt_snd) {
        *amt_snd = sendLength;
    }
#endif

#ifdef SOCKTEST_IOCTL_INQ
    result = ioctl(fd, SOCKTEST_IOCTL_INQ, &receiveLength);
    if (result < 0) {
        printf("ioctl: failed to obtain INQLEN for socket %d: %s\n", fd, strerror(errno));
        return -1;
    }
    if(amt_rcv) {
        *amt_rcv = receiveLength;
    }
#endif

    return 0;
}

static int log_sizes(int fd, int get_len, char* str) {
    unsigned int sendSize = 1, receiveSize = 1, sendLength = 1, receiveLength = 1;

    if(get_sizes(fd, &sendSize, &receiveSize) < 0) {
        return -1;
    }
    if(get_len) {
        if(get_lengths(fd, &sendLength, &receiveLength) < 0) {
            return -1;
        }
    }

    printf("%s fd=%d,snd_sz=%u,snd_len=%u,rcv_sz=%u,rcv_len=%u\n",
            str, fd, sendSize, sendLength, receiveSize, receiveLength);
    return 0;
}

int test_set_size_connect_helper(int call_connect) {
    int sd = 0, cd = 0, sd_child = 0;
    in_port_t server_port = 0;
    int result = 0;

    if(common_setup_tcp_sockets(&sd, &cd, &server_port) < 0) {
        goto fail;
    }

    if(call_connect) {
        if(common_connect_tcp_sockets(sd, cd, &sd_child, server_port) < 0) {
            goto fail;
        }
    }

    if(log_sizes(sd, 0, "before setting size: server listener") < 0) {
        goto fail;
    }
    if(log_sizes(cd, 1, "before setting size: client") < 0) {
        goto fail;
    }

    if(set_sizes(cd, 54321, 12345) < 0) {
        goto fail;
    }

    if(log_sizes(cd, 1, "after setting size: client") < 0) {
        goto fail;
    }

    unsigned int snd = 0, rcv = 0;
    if(get_sizes(cd, &snd, &rcv) < 0) {
        goto fail;
    }

    if(snd != 2*54321 || rcv != 2*12345) {
        goto fail;
    }

    close(cd);
    close(sd_child);
    close(sd);
    return 0;

fail:
    close(cd);
    close(sd_child);
    close(sd);
    return -1;
}

int do_send_receive_loop(int sd_child, int cd, int num_loops) {
    int result = 0;

    char* outbuf = calloc(1, BUFFERSIZE);
    _fillcharbuf(outbuf, BUFFERSIZE);

    int i = 0;
    for(i = 0; i < num_loops; i++) {
        log_sizes(sd_child, 1, "server child before send");
        log_sizes(cd, 1, "client before send");

        ssize_t n = send(cd, outbuf, (size_t)BUFFERSIZE, 0);
        printf("send() returned %li\n", (long)n);
        if(result < 0 && errno != EAGAIN) {
            printf("send() error was: %s\n", strerror(errno));
            return -1;
        }

        log_sizes(sd_child, 1, "server child before recv");
        log_sizes(cd, 1, "client before recv");

        n = recv(sd_child, outbuf, (size_t)BUFFERSIZE, 0);
        printf("recv() returned %li\n", (long)n);
        if(result < 0) {
            printf("recv() error was: %s\n", strerror(errno));
            return -1;
        }
    }

    return 0;
}

int test_autotune_helper(int use_autotune) {
    int sd = 0, cd = 0, sd_child = 0;
    in_port_t server_port = 0;
    int result = 0;

    if(common_setup_tcp_sockets(&sd, &cd, &server_port) < 0) {
        goto fail;
    }

    if(!use_autotune) {
        if(log_sizes(sd, 0, "before setting size: server listener") < 0) {
            goto fail;
        }
        if(log_sizes(cd, 1, "before setting size: client") < 0) {
            goto fail;
        }

        if(set_sizes(cd, 54321, 12345) < 0) {
            goto fail;
        }
        if(set_sizes(sd, 54321, 12345) < 0) {
            goto fail;
        }

        if(log_sizes(cd, 1, "after setting size: client") < 0) {
            goto fail;
        }
    }

    unsigned int srv_snd_before = 0, srv_rcv_before = 0;
    unsigned int cli_snd_before = 0, cli_rcv_before = 0;
    if(get_sizes(cd, &cli_snd_before, &cli_rcv_before) < 0) {
        goto fail;
    }
    if(get_sizes(sd, &srv_snd_before, &srv_rcv_before) < 0) {
        goto fail;
    }

    if(common_connect_tcp_sockets(sd, cd, &sd_child, server_port) < 0) {
        goto fail;
    }

    // child is not valid until after call to connect
    if(!use_autotune) {
        if(set_sizes(sd_child, 54321, 12345) < 0) {
            goto fail;
        }
    }

    unsigned int child_snd_before = 0, child_rcv_before = 0;
    if(get_sizes(sd_child, &child_snd_before, &child_rcv_before) < 0) {
        goto fail;
    }

    if(do_send_receive_loop(sd_child, cd, 10) < 0) {
        goto fail;
    }

    unsigned int srv_snd_after = 0, srv_rcv_after = 0;
    unsigned int cli_snd_after = 0, cli_rcv_after = 0;
    unsigned int child_snd_after = 0, child_rcv_after = 0;
    if(get_sizes(cd, &cli_snd_after, &cli_rcv_after) < 0) {
        goto fail;
    }
    if(get_sizes(sd, &srv_snd_after, &srv_rcv_after) < 0) {
        goto fail;
    }
    if(get_sizes(sd_child, &child_snd_after, &child_rcv_after) < 0) {
        goto fail;
    }

    printf("tcp autotuning was %s\n", use_autotune ? "enabled" : "disabled");
    printf("server: send before %i send after %i\n", srv_snd_before, srv_snd_after);
    printf("server: recv before %i recv after %i\n", srv_rcv_before, srv_rcv_after);
    printf("child: send before %i send after %i\n", child_snd_before, child_snd_after);
    printf("child: recv before %i recv after %i\n", child_rcv_before, child_rcv_after);
    printf("client: send before %i send after %i\n", cli_snd_before, cli_snd_after);
    printf("client: recv before %i recv after %i\n", cli_rcv_before, cli_rcv_after);

    if(use_autotune) {
        // the listener doesn't really get autotuned since its not transferring data
        if((cli_snd_after <= cli_snd_before && cli_rcv_after <= cli_rcv_before) ||
                (child_snd_after <= child_snd_before && child_rcv_after <= child_rcv_before)) {
            printf("failed - the buffer should have increased with autotuning\n");
            goto fail;
        }
    } else {
        if(cli_snd_after != cli_snd_before || srv_rcv_after != srv_rcv_before || child_rcv_after != child_rcv_before) {
            printf("failed - the buffer size should be the same since autotuning is disabled\n");
            goto fail;
        }
    }

    close(cd);
    close(sd_child);
    close(sd);
    return 0;

fail:
    close(cd);
    close(sd_child);
    close(sd);
    return -1;
}

int test_set_size_before_connect(){
    printf("########## running test_set_size_before_connect\n");
    return test_set_size_connect_helper(0);
}

int test_set_size_after_connect(){
    printf("########## running test_set_size_after_connect\n");
    return test_set_size_connect_helper(1);
}

int test_set_size_to_disable_autotune() {
    printf("########## running test_set_size_to_disable_autotune\n");
    return test_autotune_helper(0);
}

int test_autotune_increases_size() {
    printf("########## running test_autotune_increases_size\n");
    return test_autotune_helper(1);
}

int run() {
    if(test_set_size_before_connect() < 0) {
        return EXIT_FAILURE;
    }

    if(test_set_size_after_connect() < 0) {
        return EXIT_FAILURE;
    }

    if(test_set_size_to_disable_autotune() < 0) {
        return EXIT_FAILURE;
    }

    if(test_autotune_increases_size() < 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int main() {
    printf("########## sockbuf test starting ##########\n");

    if(run() == EXIT_SUCCESS) {
        printf("########## sockbuf test passed ##########\n");
        return EXIT_SUCCESS;
    } else {
        printf("########## sockbuf test failed ##########\n");
        return EXIT_FAILURE;
    }
}
