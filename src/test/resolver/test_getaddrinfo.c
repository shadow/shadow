#include <glib.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "test/test_common.h"

#define STRINGIFY_ENUM_CASE(buf, e)                                            \
    case e:                                                                    \
        sprintf(buf, #e "(%d)", e);                                            \
        break;
#define STRINGIFY_ENUM_DEFAULT(buf, d)                                         \
    default: sprintf(buf, "unknown(%d)", (int)d); break;

const char* socktype_string(int socktype) {
    static char buf[100];
    switch (socktype) {
        STRINGIFY_ENUM_CASE(buf, SOCK_STREAM);
        STRINGIFY_ENUM_CASE(buf, SOCK_DGRAM);
        STRINGIFY_ENUM_CASE(buf, SOCK_RAW);
        STRINGIFY_ENUM_DEFAULT(buf, socktype);
    }
    return buf;
}

const char* family_string(int family) {
    static char buf[100];
    switch (family) {
        STRINGIFY_ENUM_CASE(buf, AF_UNSPEC);
        STRINGIFY_ENUM_CASE(buf, AF_INET);
        STRINGIFY_ENUM_CASE(buf, AF_INET6);
        STRINGIFY_ENUM_DEFAULT(buf, family);
    }
    return buf;
}

const char* protocol_string(int protocol) {
    static char buf[100];
    switch (protocol) {
        STRINGIFY_ENUM_CASE(buf, IPPROTO_UDP);
        STRINGIFY_ENUM_CASE(buf, IPPROTO_TCP);
        STRINGIFY_ENUM_DEFAULT(buf, protocol);
    }
    return buf;
}

const char* getaddrinfo_rv_string(char* buf, int protocol) {
    switch (protocol) {
        STRINGIFY_ENUM_CASE(buf, EAI_ADDRFAMILY);
        STRINGIFY_ENUM_CASE(buf, EAI_AGAIN);
        STRINGIFY_ENUM_CASE(buf, EAI_BADFLAGS);
        STRINGIFY_ENUM_CASE(buf, EAI_FAIL);
        STRINGIFY_ENUM_CASE(buf, EAI_FAMILY);
        STRINGIFY_ENUM_CASE(buf, EAI_MEMORY);
        STRINGIFY_ENUM_CASE(buf, EAI_NODATA);
        STRINGIFY_ENUM_CASE(buf, EAI_NONAME);
        STRINGIFY_ENUM_CASE(buf, EAI_SERVICE);
        STRINGIFY_ENUM_CASE(buf, EAI_SOCKTYPE);
        STRINGIFY_ENUM_CASE(buf, EAI_SYSTEM);
        STRINGIFY_ENUM_DEFAULT(buf, protocol);
    }
    return buf;
}

int addrinfo_count(struct addrinfo* res) {
    int n = 0;
    while (res != NULL) {
        ++n;
        res = res->ai_next;
    }
    return n;
}

const char* sockaddr_in_string(const struct sockaddr* addr) {
    static char ip[20] = {0};
    static char ip_and_port[30] = {0};
    // INET6 unhandled
    if (addr->sa_family == AF_INET) {
        const struct sockaddr_in* addr_in = (const struct sockaddr_in*)addr;
        inet_ntop(AF_INET, &addr_in->sin_addr, ip, 20);
        sprintf(ip_and_port, "%s:%d", ip, ntohs(addr_in->sin_port));
    } else if (addr->sa_family == AF_INET6) {
        const struct sockaddr_in6* addr_in6 = (const struct sockaddr_in6*)addr;
        inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip, 20);
        sprintf(ip_and_port, "%s:%d", ip, ntohs(addr_in6->sin6_port));
    } else {
        sprintf(ip_and_port, "<Unknown addr family %d>", addr->sa_family);
    }
    return ip_and_port;
}

#define STR_OR_NULL(x) ((x) == NULL) ? "(null)" : (x)

void addrinfo_print(const struct addrinfo* res) {
    while (res != NULL) {
        printf("{\n");
        printf("  ai_flags: %d\n", res->ai_flags);
        printf("  ai_family: %s\n", family_string(res->ai_family));
        printf("  ai_socktype: %s\n", socktype_string(res->ai_socktype));
        printf("  ai_protocol: %s\n", protocol_string(res->ai_protocol));
        printf("  ai_addrlen: %d\n", (int)res->ai_addrlen);
        printf("  ai_addr: %s\n", sockaddr_in_string(res->ai_addr));
        printf("  ai_canonname: %s\n", STR_OR_NULL(res->ai_canonname));
        printf("  ai_next: %p\n", res->ai_next);
        printf("}\n");
        res = res->ai_next;
    }
}

bool sockaddr_equals(const struct sockaddr* lhs, const struct sockaddr* rhs) {
    if (lhs == NULL && rhs == NULL)
        return true;
    if (lhs == NULL || rhs == NULL)
        return false;
    if (lhs->sa_family != rhs->sa_family)
        return false;
    // INET6 unhandled
    g_assert(lhs->sa_family == AF_INET);
    const struct sockaddr_in* lhs_in = (const struct sockaddr_in*)lhs;
    const struct sockaddr_in* rhs_in = (const struct sockaddr_in*)rhs;
    return !memcmp(lhs_in, rhs_in, sizeof(*lhs_in));
}

bool addrinfo_equals(const struct addrinfo* lhs, const struct addrinfo* rhs) {
    while (lhs != NULL && rhs != NULL) {
        if (lhs->ai_flags != rhs->ai_flags ||
            lhs->ai_family != rhs->ai_family ||
            lhs->ai_socktype != rhs->ai_socktype ||
            lhs->ai_protocol != rhs->ai_protocol ||
            lhs->ai_addrlen != rhs->ai_addrlen ||
            !sockaddr_equals(lhs->ai_addr, rhs->ai_addr))
            return false;
        if (lhs->ai_canonname != NULL || rhs->ai_canonname != NULL) {
            if (lhs->ai_canonname == NULL || rhs->ai_canonname == NULL)
                return false;
            if (strcmp(lhs->ai_canonname, rhs->ai_canonname))
                return false;
        }
        lhs = lhs->ai_next;
        rhs = rhs->ai_next;
    }
    return lhs == NULL && rhs == NULL;
}

#define assert_addrinfo_equals(got, expected)                                  \
    if (!addrinfo_equals(got, expected)) {                                     \
        printf("Expected:\n");                                                 \
        addrinfo_print(expected);                                              \
        printf("Got:\n");                                                      \
        addrinfo_print(got);                                                   \
        g_test_fail();                                                         \
    }

#define assert_getaddrinfo_rv_equals(got, expected)                                                \
    {                                                                                              \
        char buf1_##__LINE__[20];                                                                  \
        char buf2_##__LINE__[20];                                                                  \
        int rv##__LINE__ = got;                                                                    \
        if (rv##__LINE__ != expected) {                                                            \
            printf("Expected: %s ; Got: %s ; errno: %s\n",                                         \
                   getaddrinfo_rv_string(buf1_##__LINE__, expected),                               \
                   getaddrinfo_rv_string(buf2_##__LINE__, rv##__LINE__),                           \
                   (rv##__LINE__ == EAI_SYSTEM) ? strerror(errno) : "N/A");                        \
            /* Abort the rest of the test to avoid accessing an invalid addrinfo pointer */        \
            g_test_fail();                                                                         \
            return;                                                                                \
        }                                                                                          \
    }

void test_service() {
    // Numeric service with a single result as baseline.
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo* res;
    assert_getaddrinfo_rv_equals(getaddrinfo(NULL, "80", &hints, &res), 0);
    const struct sockaddr_in expected_sockaddr_in = {
        .sin_family = AF_INET,
        .sin_port = htons(80),
        .sin_addr = {htonl(INADDR_LOOPBACK)}};
    struct addrinfo expected_tcp_addrinfo = {
        .ai_flags = 0,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_addrlen = sizeof(expected_sockaddr_in),
        .ai_addr = (struct sockaddr*)&expected_sockaddr_in,
    };
    assert_addrinfo_equals(res, &expected_tcp_addrinfo);
    freeaddrinfo(res);

    // Restricting the protocol instead of the socktype should give the same
    // result.
    hints = (struct addrinfo){.ai_family = AF_INET, .ai_protocol = IPPROTO_TCP};
    assert_getaddrinfo_rv_equals(getaddrinfo(NULL, "80", &hints, &res), 0);
    assert_addrinfo_equals(res, &expected_tcp_addrinfo);
    freeaddrinfo(res);

    // Specifying the protocol by name instead of number should give us the
    // same result.
    assert_getaddrinfo_rv_equals(getaddrinfo(NULL, "http", &hints, &res), 0);
    assert_addrinfo_equals(res, &expected_tcp_addrinfo);
    freeaddrinfo(res);

    // A non-existent service should fail
    assert_getaddrinfo_rv_equals(
        getaddrinfo(NULL, "jrX-9Z~Ay8", NULL, &res), EAI_SERVICE);

    // Specifying protocol by name with AI_NUMERICSERV should fail:
    hints = (struct addrinfo){.ai_flags = AI_NUMERICSERV};
    assert_getaddrinfo_rv_equals(
        getaddrinfo(NULL, "http", &hints, &res), EAI_NONAME);

    // Specifying datagram should give us UDP.
    hints = (struct addrinfo){.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM};
    struct addrinfo expected_udp_addrinfo = {
        .ai_flags = 0,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP,
        .ai_addrlen = sizeof(expected_sockaddr_in),
        .ai_addr = (struct sockaddr*)&expected_sockaddr_in,
    };
    assert_getaddrinfo_rv_equals(getaddrinfo(NULL, "80", &hints, &res), 0);
    assert_addrinfo_equals(res, &expected_udp_addrinfo);
    freeaddrinfo(res);

    // Likewise for specifying UDP:
    hints = (struct addrinfo){.ai_family = AF_INET, .ai_protocol = IPPROTO_UDP};
    assert_getaddrinfo_rv_equals(getaddrinfo(NULL, "80", &hints, &res), 0);
    assert_addrinfo_equals(res, &expected_udp_addrinfo);
    freeaddrinfo(res);

    // If we don't restrict the protocol, we should get a list of TCP, UDP, and
    // RAW, in that order.
    struct addrinfo expected_raw_addrinfo = {
        .ai_flags = 0,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_RAW,
        .ai_protocol = 0,
        .ai_addrlen = sizeof(expected_sockaddr_in),
        .ai_addr = (struct sockaddr*)&expected_sockaddr_in,
    };
    expected_tcp_addrinfo.ai_next = &expected_udp_addrinfo;
    expected_udp_addrinfo.ai_next = &expected_raw_addrinfo;
    hints = (struct addrinfo){.ai_family = AF_INET};
    res = NULL;
    assert_getaddrinfo_rv_equals(getaddrinfo(NULL, "80", &hints, &res), 0);
    assert_addrinfo_equals(res, &expected_tcp_addrinfo);
    freeaddrinfo(res);
}

void test_numeric_host() {
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo* res;
    uint64_t addr;
    g_assert(inet_pton(AF_INET, "1.2.3.4", &addr) == 1);
    const struct sockaddr_in expected_sockaddr_in = {
        .sin_family = AF_INET, .sin_addr = {addr}};
    struct addrinfo expected_addrinfo = {
        .ai_flags = 0,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_addrlen = sizeof(expected_sockaddr_in),
        .ai_addr = (struct sockaddr*)&expected_sockaddr_in,
    };
    assert_getaddrinfo_rv_equals(getaddrinfo("1.2.3.4", NULL, &hints, &res), 0);
    assert_addrinfo_equals(res, &expected_addrinfo);
    freeaddrinfo(res);

    // Error on nonnumeric node with AI_NUMERICHOST
    hints = (struct addrinfo){.ai_flags = AI_NUMERICHOST};
    assert_getaddrinfo_rv_equals(
        getaddrinfo("localhost", NULL, &hints, &res), EAI_NONAME);
}

void test_host_file() {
    // Don't know of a way to inject a fake /etc/hosts file (outside of
    // Shadow), so we just check "localhost", which we can expect to be there.
    // This at least validates that the hosts file is being checked, and we can
    // use more focused unit-testing on the hosts-file-parsing code.
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo* res;
    const struct sockaddr_in expected_sockaddr_in = {
        .sin_family = AF_INET, .sin_addr = {htonl(INADDR_LOOPBACK)}};
    struct addrinfo expected_addrinfo = {
        .ai_flags = 0,
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
        .ai_addrlen = sizeof(expected_sockaddr_in),
        .ai_addr = (struct sockaddr*)&expected_sockaddr_in,
    };
    assert_getaddrinfo_rv_equals(
        getaddrinfo("localhost", NULL, &hints, &res), 0);

    // skip this check on linux since this may return two duplicate entries depending on the hosts
    // file: https://stackoverflow.com/a/39538935
    if (running_in_shadow()) {
        assert_addrinfo_equals(res, &expected_addrinfo);
    }

    freeaddrinfo(res);
}

void test_ipv6() {
    struct addrinfo hints = {
        .ai_family = AF_INET6, .ai_socktype = SOCK_STREAM, .ai_flags = AI_PASSIVE};
    struct addrinfo* res;

    int rv = getaddrinfo(NULL, "80", &hints, &res);

    if (running_in_shadow()) {
        // shadow doesn't support IPv6
        assert_getaddrinfo_rv_equals(rv, EAI_NONAME);
    } else {
        // linux should return a non-error result
        assert_getaddrinfo_rv_equals(rv, 0);
    }
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);

    // Define the tests.
    g_test_add_func("/getaddrinfo/service", &test_service);
    g_test_add_func("/getaddrinfo/numeric_host", &test_numeric_host);
    g_test_add_func("/getaddrinfo/host_file", &test_host_file);
    g_test_add_func("/getaddrinfo/ipv6", &test_ipv6);

    return g_test_run();
}
