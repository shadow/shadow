/* Wrapper header file that we pass to bindgen.
 * See `gen-kernel-bindings.sh`.
 */

#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/rseq.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/sockios.h>
#include <linux/time.h>

#include <asm/ioctls.h>