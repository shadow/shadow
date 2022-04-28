/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TIMER_H_
#define SHD_TIMER_H_

#include <glib.h>
#include <sys/types.h>

#include "main/core/support/definitions.h"

typedef struct _TimerFd TimerFd;

/* free this with descriptor_free() */
TimerFd* timerfd_new();
gint timerfd_setTime(TimerFd* timer, Host* host, gint flags, const struct itimerspec* new_value,
                     struct itimerspec* old_value);
gint timerfd_getTime(const TimerFd* timer, struct itimerspec* curr_value);
ssize_t timerfd_read(TimerFd* timer, void* buf, size_t count);

/* Returns the number of timer expirations that have occurred
 * since the last time the timer was set. */
guint64 timerfd_getExpirationCount(const TimerFd* timer);

#endif /* SHD_TIMER_H_ */
