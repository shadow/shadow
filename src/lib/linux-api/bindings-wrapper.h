/* Wrapper header file that we pass to bindgen.
 * See `gen-kernel-bindings.sh`.
 */

#include <linux/capability.h>
#include <linux/close_range.h>
#include <linux/errno.h>
#include <linux/eventpoll.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/in.h>
#include <linux/limits.h>
#include <linux/mman.h>
#include <linux/netlink.h>
#include <linux/prctl.h>
#include <linux/resource.h>
#include <linux/rseq.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/sockios.h>
#include <linux/stat.h>
#include <linux/time.h>
#include <linux/time_types.h>
#include <linux/unistd.h>
#include <linux/utsname.h>
#include <linux/wait.h>

#include <asm/ioctls.h>
#include <asm/sigcontext.h>
// linux defines "struct stat" differently in both "asm/stat.h" and "asm-generic/stat.h"; not sure
// which we're supposed to use, but "asm/stat.h" *seems* to be the right version for x86-64, since
// it seems to better match glibc's "struct stat" in "/usr/include/bits/struct_stat.h" ("st_nlink"
// is ordered before "st_mode").
#include <asm/stat.h>
#include <asm/ucontext.h>
#include <asm/unistd_64.h>

#include <asm-generic/poll.h>

/* Epoll flags from eventpoll.h. These seem to be missed because of an inline
 * cast to `(__poll_t)`, so we add them here manually after cleaning up.
 */
#define EPOLLIN        0x00000001
#define EPOLLPRI       0x00000002
#define EPOLLOUT       0x00000004
#define EPOLLERR       0x00000008
#define EPOLLHUP       0x00000010
#define EPOLLNVAL      0x00000020
#define EPOLLRDNORM    0x00000040
#define EPOLLRDBAND    0x00000080
#define EPOLLWRNORM    0x00000100
#define EPOLLWRBAND    0x00000200
#define EPOLLMSG       0x00000400
#define EPOLLRDHUP     0x00002000
#define EPOLLEXCLUSIVE 0x10000000
#define EPOLLWAKEUP    0x20000000
#define EPOLLONESHOT   0x40000000
#define EPOLLET        0x80000000

// SUID_DUMP_* flags that aren't exposed in kernel headers.
// https://elixir.bootlin.com/linux/v6.6.9/source/include/linux/sched/coredump.h#L7
#define SUID_DUMP_DISABLE   0
#define SUID_DUMP_USER      1
#define SUID_DUMP_ROOT      2

// socket domains aren't exposed in kernel headers.
#define AF_UNSPEC 0
#define AF_UNIX 1
#define AF_LOCAL 1
#define AF_INET 2
#define AF_AX25 3
#define AF_IPX 4
#define AF_APPLETALK 5
#define AF_NETROM 6
#define AF_BRIDGE 7
#define AF_ATMPVC 8
#define AF_X25 9
#define AF_INET6 10
#define AF_ROSE 11
#define AF_DECnet 12
#define AF_NETBEUI 13
#define AF_SECURITY 14
#define AF_KEY 15
#define AF_NETLINK 16
#define AF_ROUTE AF_NETLINK
#define AF_PACKET 17
#define AF_ASH 18
#define AF_ECONET 19
#define AF_ATMSVC 20
#define AF_RDS 21
#define AF_SNA 22
#define AF_IRDA 23
#define AF_PPPOX 24
#define AF_WANPIPE 25
#define AF_LLC 26
#define AF_IB 27
#define AF_MPLS 28
#define AF_CAN 29
#define AF_TIPC 30
#define AF_BLUETOOTH 31
#define AF_IUCV 32
#define AF_RXRPC 33
#define AF_ISDN 34
#define AF_PHONET 35
#define AF_IEEE802154 36
#define AF_CAIF 37
#define AF_ALG 38
#define AF_NFC 39
#define AF_VSOCK 40
#define AF_KCM 41
#define AF_QIPCRTR 42
#define AF_SMC 43
#define AF_XDP 44
#define AF_MCTP 45

// socket shutdown commands aren't exposed in kernel headers.
// copied from kernel source linux/net.h
enum sock_shutdown_cmd {
    SHUT_RD,
    SHUT_WR,
    SHUT_RDWR,
};
