#include <glib.h>
#include <linux/rtnetlink.h>
#include <stddef.h>
#include <sys/socket.h>

#include "test/test_glib_helpers.h"

static int flood(int fd) {
    int ret;
    struct {
        struct nlmsghdr  hdr;
        struct ifaddrmsg msg;
    } req;
    req.msg = (struct ifaddrmsg) {
        .ifa_family = AF_UNSPEC,
        .ifa_prefixlen = 0,
        .ifa_flags = 0,
        .ifa_scope = RT_SCOPE_UNIVERSE,
        .ifa_index = 0,
    };
    int len = NLMSG_LENGTH(sizeof(req.msg));
    req.hdr = (struct nlmsghdr) {
        .nlmsg_len = len,
        .nlmsg_type = RTM_GETADDR,
        .nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP,
        .nlmsg_seq = 0xfe182ab9,
        .nlmsg_pid = 0,
    };

    for (int i = 0; i <= 8192/len /*8KB*/; i++) {
        ret = sendto(fd, &req, sizeof(req), 0, NULL, 0);
        if (ret == -1) {
            return ret;
        }
    }
    return 0;
}

static void test_send_limit_not_exceed() {
    int fd = socket(AF_NETLINK, SOCK_RAW|SOCK_NONBLOCK, NETLINK_ROUTE);
    assert_nonneg_errno(flood(fd));
}

static void test_send_limit_exceed() {
    int fd = socket(AF_NETLINK, SOCK_RAW|SOCK_NONBLOCK, NETLINK_ROUTE);
    __u32 limit = 2048;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &limit, sizeof(limit));
    // It will reach the limit
    assert_true_errno(flood(fd) == -1);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/netlink/send_limit_not_exceed", test_send_limit_not_exceed);
    g_test_add_func("/netlink/send_limit_exceed", test_send_limit_exceed);
    return g_test_run();
}
