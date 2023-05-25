#ifndef LINUX_API_BINDINGS_WRAPPER_H
#define LINUX_API_BINDINGS_WRAPPER_H
/* Wrapper header file that we pass to bindgen.
 * See `gen-kernel-bindings.sh`.
 *
 * We also include this in generated C bindings, since exported C functions need
 * the same definitions.
 */

#include <stddef.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/time.h>

#endif