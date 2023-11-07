/* Wrapper header file that we pass to bindgen.
 * See `gen-kernel-bindings.sh`.
 */

#include <linux/errno.h>
#include <linux/eventpoll.h>
#include <linux/fcntl.h>
#include <linux/futex.h>
#include <linux/in.h>
#include <linux/limits.h>
#include <linux/mman.h>
#include <linux/prctl.h>
#include <linux/resource.h>
#include <linux/rseq.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/sockios.h>
#include <linux/time.h>
#include <linux/time_types.h>
#include <linux/unistd.h>
#include <linux/utsname.h>
#include <linux/wait.h>

#include <asm/ioctls.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>

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
