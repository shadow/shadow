/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_TIMER_H_
#define SHD_TIMER_H_

#include <glib.h>
#include <sys/types.h>

typedef struct _Timer Timer;

/* free this with descriptor_free() */
Timer* timer_new();
gint timer_setTime(Timer* timer, gint flags,
                   const struct itimerspec *new_value,
                   struct itimerspec *old_value);
gint timer_getTime(Timer* timer, struct itimerspec *curr_value);
ssize_t timer_read(Timer* timer, void *buf, size_t count);
gint timer_close(Timer* timer);

/* Returns the number of timer expirations that have occurred
 * since the last time the timer was set. */
guint64 timer_getExpirationCount(Timer* timer);

#endif /* SHD_TIMER_H_ */
