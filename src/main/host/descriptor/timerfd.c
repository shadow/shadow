/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/timerfd.h"

#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <time.h>

#include "lib/logger/logger.h"
#include "main/core/support/definitions.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/host.h"
#include "main/host/timer.h"
#include "main/utility/utility.h"

struct _TimerFd {
    LegacyDescriptor super;
    Timer* timer;
    gboolean isClosed;

    MAGIC_DECLARE;
};

static TimerFd* _timerfd_fromLegacyDescriptor(LegacyDescriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_TIMER);
    return (TimerFd*)descriptor;
}

static void _timerfd_close(LegacyDescriptor* descriptor, Host* host) {
    TimerFd* timerfd = _timerfd_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(timerfd);
    trace("timer fd %i closing now", timerfd->super.handle);
    timerfd->isClosed = TRUE;
    descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_ACTIVE, FALSE);
}

static void _timerfd_free(LegacyDescriptor* descriptor) {
    TimerFd* timerfd = _timerfd_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(timerfd);
    descriptor_clear((LegacyDescriptor*)timerfd);
    if (timerfd->timer) {
        timer_unref(timerfd->timer);
        timerfd->timer = NULL;
    }
    MAGIC_CLEAR(timerfd);
    g_free(timerfd);
    worker_count_deallocation(TimerFd);
}

static void _timerfd_cleanup(LegacyDescriptor* descriptor) {
    TimerFd* timerfd = _timerfd_fromLegacyDescriptor(descriptor);
    MAGIC_ASSERT(timerfd);

    if (timerfd->timer) {
        // Break circular reference; the timer has a task with a reference to
        // this descriptor.
        timer_unref(timerfd->timer);
        timerfd->timer = NULL;
    }
}

static DescriptorFunctionTable _timerfdFunctions = {
    _timerfd_close, _timerfd_cleanup, _timerfd_free, MAGIC_VALUE};

static void _timerfd_expire(Host* host, gpointer voidTimer, gpointer data);

TimerFd* timerfd_new() {
    TimerFd* timerfd = g_new0(TimerFd, 1);
    MAGIC_INIT(timerfd);

    descriptor_init(&(timerfd->super), DT_TIMER, &_timerfdFunctions);
    descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_ACTIVE, TRUE);

    descriptor_refWeak(timerfd);
    Task* task = task_new(_timerfd_expire, timerfd, NULL, descriptor_unrefWeak, NULL);
    timerfd->timer = timer_new(task);
    task_unref(task);

    worker_count_allocation(TimerFd);

    return timerfd;
}

void timerfd_getTime(const TimerFd* timerfd, struct itimerspec* curr_value) {
    MAGIC_ASSERT(timerfd);
    utility_assert(curr_value);

    SimulationTime remainingTime = timer_getRemainingTime(timerfd->timer);
    utility_assert(remainingTime != SIMTIME_INVALID);
    if (!simtime_to_timespec(remainingTime, &curr_value->it_value)) {
        panic("Couldn't convert %ld", remainingTime);
    }

    SimulationTime interval = timer_getInterval(timerfd->timer);
    if (!simtime_to_timespec(interval, &curr_value->it_interval)) {
        panic("Couldn't convert %ld", interval);
    }
}

static void _timerfd_expire(Host* host, gpointer voidTimerFd, gpointer data) {
    TimerFd* timerfd = voidTimerFd;
    MAGIC_ASSERT(timerfd);

    if (!timerfd->isClosed) {
        descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_READABLE, TRUE);
    }
}

static void _timerfd_arm(TimerFd* timerfd, Host* host, const struct itimerspec* config,
                         gint flags) {
    MAGIC_ASSERT(timerfd);
    utility_assert(config);

    SimulationTime configSimTime = simtime_from_timespec(config->it_value);
    utility_assert(configSimTime != SIMTIME_INVALID);

    EmulatedTime now = worker_getCurrentEmulatedTime();
    EmulatedTime base = (flags == TFD_TIMER_ABSTIME) ? EMUTIME_UNIX_EPOCH : now;
    EmulatedTime nextExpireTime = base + configSimTime;
    /* the man page does not specify what happens if the time
     * they gave us is in the past. on linux, the result is an
     * immediate timer expiration. */
    if (nextExpireTime < now) {
        nextExpireTime = now;
    }

    SimulationTime interval = simtime_from_timespec(config->it_interval);

    timer_arm(timerfd->timer, host, nextExpireTime, interval);

    trace("timer fd %i armed to expire in %" G_GUINT64_FORMAT " nanos", timerfd->super.handle,
          nextExpireTime - now);
}

static gboolean _timerfd_timeIsValid(const struct timespec* config) {
    utility_assert(config);
    return (config->tv_nsec < 0 || config->tv_nsec >= SIMTIME_ONE_SECOND) ? FALSE : TRUE;
}

gint timerfd_setTime(TimerFd* timerfd, Host* host, gint flags, const struct itimerspec* new_value,
                     struct itimerspec* old_value) {
    MAGIC_ASSERT(timerfd);

    utility_assert(new_value);

    if (!_timerfd_timeIsValid(&(new_value->it_value)) ||
        !_timerfd_timeIsValid(&(new_value->it_interval))) {
        return -EINVAL;
    }

    if(flags != 0 && flags != TFD_TIMER_ABSTIME) {
        return -EINVAL;
    }

    trace("Setting timer value to "
          "%" G_GUINT64_FORMAT ".%09" G_GUINT64_FORMAT " seconds "
          "and timer interval to "
          "%" G_GUINT64_FORMAT ".%09" G_GUINT64_FORMAT " seconds "
          "on timer fd %d",
          new_value->it_value.tv_sec, new_value->it_value.tv_nsec, new_value->it_interval.tv_sec,
          new_value->it_interval.tv_nsec, timerfd->super.handle);

    /* first get the old value if requested */
    if (old_value) {
        /* old value is always relative, even if TFD_TIMER_ABSTIME is set */
        timerfd_getTime(timerfd, old_value);
    }

    /* settings were modified, reset readability */
    descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_READABLE, FALSE);

    /* now set the new times as requested */
    if (new_value->it_value.tv_sec == 0 && new_value->it_value.tv_nsec == 0) {
        /* A value of 0 disarms the timer; it_interval is ignored. */
        timer_disarm(timerfd->timer);
    } else {
        _timerfd_arm(timerfd, host, new_value, flags);
    }

    return 0;
}

ssize_t timerfd_read(TimerFd* timerfd, void* buf, size_t count) {
    MAGIC_ASSERT(timerfd);

    guint64 expirationCount = timer_consumeExpirationCount(timerfd->timer);
    if (expirationCount > 0) {
        /* we have something to report, make sure the buf is big enough */
        if (count < sizeof(guint64)) {
            return (ssize_t)-EINVAL;
        }

        trace("Reading %" G_GUINT64_FORMAT " expirations from timer fd %d", expirationCount,
              timerfd->super.handle);

        *(guint64*)buf = expirationCount;

        /* reset readability */
        descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_READABLE, FALSE);

        return (ssize_t)sizeof(guint64);
    } else {
        /* the timer has not yet expired, try again later */
        return (ssize_t)-EWOULDBLOCK;
    }
}

guint64 timerfd_getExpirationCount(const TimerFd* timerfd) {
    MAGIC_ASSERT(timerfd);
    return timer_getExpirationCount(timerfd->timer);
}
