/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <limits.h>
#include <net/if.h>
#include <netdb.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int shim_api_getifaddrs(struct ifaddrs** ifap) {
    if (!ifap) {
        errno = EFAULT;
        return -1;
    }

    /* we always have loopback */
    struct ifaddrs* i = calloc(1, sizeof(struct ifaddrs));
    i->ifa_flags = (IFF_UP | IFF_RUNNING | IFF_LOOPBACK);
    i->ifa_name = strdup("lo");

    i->ifa_addr = calloc(1, sizeof(struct sockaddr));
    i->ifa_addr->sa_family = AF_INET;

    struct in_addr addr_buf;
    if (inet_pton(AF_INET, "127.0.0.1", &addr_buf) == 1) {
        ((struct sockaddr_in*)i->ifa_addr)->sin_addr = addr_buf;
    } else {
        errno = EADDRNOTAVAIL;
        return -1;
    }

    /* get the hostname so we can use it to lookup the default net address */
    char hostname_buf[HOST_NAME_MAX] = {};
    if (gethostname(hostname_buf, HOST_NAME_MAX) == 0) {
        struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
        struct addrinfo* host_ai;

        /* lookup the default net address for the host */
        if (getaddrinfo(hostname_buf, NULL, &hints, &host_ai) == 0) {
            struct ifaddrs* j = calloc(1, sizeof(struct ifaddrs));
            j->ifa_flags = (IFF_UP | IFF_RUNNING);
            j->ifa_name = strdup("eth0");

            j->ifa_addr = calloc(1, sizeof(struct sockaddr));
            memcpy(j->ifa_addr, host_ai->ai_addr, (unsigned long)host_ai->ai_addrlen);

            i->ifa_next = j;

            freeaddrinfo(host_ai);
        }
    }

    *ifap = i;
    return 0;
}

void shim_api_freeifaddrs(struct ifaddrs* ifa) {
    struct ifaddrs* iter = ifa;
    while (iter != NULL) {
        struct ifaddrs* next = iter->ifa_next;
        if (iter->ifa_addr) {
            free(iter->ifa_addr);
        }
        if (iter->ifa_name) {
            free(iter->ifa_name);
        }
        free(iter);
        iter = next;
    }
}
