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
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/host.h"
#include "main/utility/utility.h"

struct _TimerFd {
    LegacyFile super;
    Timer* timer;
    gboolean isClosed;

    MAGIC_DECLARE;
};

static TimerFd* _timerfd_fromLegacyFile(LegacyFile* descriptor) {
    utility_debugAssert(legacyfile_getType(descriptor) == DT_TIMER);
    return (TimerFd*)descriptor;
}

static void _timerfd_close(LegacyFile* descriptor, const Host* host) {
    TimerFd* timerfd = _timerfd_fromLegacyFile(descriptor);
    MAGIC_ASSERT(timerfd);
    trace("timer desc %p closing now", &timerfd->super);
    timerfd->isClosed = TRUE;
    legacyfile_adjustStatus(&(timerfd->super), STATUS_FILE_ACTIVE, FALSE);
}

static void _timerfd_free(LegacyFile* descriptor) {
    TimerFd* timerfd = _timerfd_fromLegacyFile(descriptor);
    MAGIC_ASSERT(timerfd);
    legacyfile_clear((LegacyFile*)timerfd);
    if (timerfd->timer) {
        timer_drop(timerfd->timer);
        timerfd->timer = NULL;
    }
    MAGIC_CLEAR(timerfd);
    g_free(timerfd);
    worker_count_deallocation(TimerFd);
}

static void _timerfd_cleanup(LegacyFile* descriptor) {
    TimerFd* timerfd = _timerfd_fromLegacyFile(descriptor);
    MAGIC_ASSERT(timerfd);

    if (timerfd->timer) {
        // Break circular reference; the timer has a task with a reference to
        // this descriptor.
        timer_drop(timerfd->timer);
        timerfd->timer = NULL;
    }
}

static LegacyFileFunctionTable _timerfdFunctions = {
    _timerfd_close, _timerfd_cleanup, _timerfd_free, MAGIC_VALUE};

static void _timerfd_expire(const Host* host, gpointer voidTimer, gpointer data);

TimerFd* timerfd_new(HostId hostId) {
    TimerFd* timerfd = g_new0(TimerFd, 1);
    MAGIC_INIT(timerfd);

    legacyfile_init(&(timerfd->super), DT_TIMER, &_timerfdFunctions);
    legacyfile_adjustStatus(&(timerfd->super), STATUS_FILE_ACTIVE, TRUE);

    legacyfile_refWeak(timerfd);
    TaskRef* task =
        taskref_new_bound(hostId, _timerfd_expire, timerfd, NULL, legacyfile_unrefWeak, NULL);
    timerfd->timer = timer_new(task);
    taskref_drop(task);

    worker_count_allocation(TimerFd);

    return timerfd;
}

void timerfd_getTime(const TimerFd* timerfd, struct itimerspec* curr_value) {
    MAGIC_ASSERT(timerfd);
    utility_debugAssert(curr_value);

    CSimulationTime remainingTime = timer_getRemainingTime(timerfd->timer);
    utility_debugAssert(remainingTime != SIMTIME_INVALID);
    if (!simtime_to_timespec(remainingTime, &curr_value->it_value)) {
        panic("Couldn't convert %ld", remainingTime);
    }

    CSimulationTime interval = timer_getInterval(timerfd->timer);
    if (!simtime_to_timespec(interval, &curr_value->it_interval)) {
        panic("Couldn't convert %ld", interval);
    }
}

static void _timerfd_expire(const Host* host, gpointer voidTimerFd, gpointer data) {
    TimerFd* timerfd = voidTimerFd;
    MAGIC_ASSERT(timerfd);

    if (!timerfd->isClosed) {
        legacyfile_adjustStatus(&(timerfd->super), STATUS_FILE_READABLE, TRUE);
    }
}

static void _timerfd_arm(TimerFd* timerfd, const Host* host, const struct itimerspec* config,
                         gint flags) {
    MAGIC_ASSERT(timerfd);
    utility_debugAssert(config);

    CSimulationTime configSimTime = simtime_from_timespec(config->it_value);
    utility_debugAssert(configSimTime != SIMTIME_INVALID);

    CEmulatedTime now = worker_getCurrentEmulatedTime();
    CEmulatedTime base = (flags == TFD_TIMER_ABSTIME) ? EMUTIME_UNIX_EPOCH : now;
    CEmulatedTime nextExpireTime = base + configSimTime;
    /* the man page does not specify what happens if the time
     * they gave us is in the past. on linux, the result is an
     * immediate timer expiration. */
    if (nextExpireTime < now) {
        nextExpireTime = now;
    }

    CSimulationTime interval = simtime_from_timespec(config->it_interval);

    timer_arm(timerfd->timer, host, nextExpireTime, interval);

    trace("timer desc %p armed to expire in %" G_GUINT64_FORMAT " nanos", &timerfd->super,
          nextExpireTime - now);
}

static gboolean _timerfd_timeIsValid(const struct timespec* config) {
    utility_debugAssert(config);
    return (config->tv_nsec < 0 || config->tv_nsec >= SIMTIME_ONE_SECOND) ? FALSE : TRUE;
}

gint timerfd_setTime(TimerFd* timerfd, const Host* host, gint flags,
                     const struct itimerspec* new_value, struct itimerspec* old_value) {
    MAGIC_ASSERT(timerfd);

    utility_debugAssert(new_value);

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
          "on timer desc %p",
          new_value->it_value.tv_sec, new_value->it_value.tv_nsec, new_value->it_interval.tv_sec,
          new_value->it_interval.tv_nsec, &timerfd->super);

    /* first get the old value if requested */
    if (old_value) {
        /* old value is always relative, even if TFD_TIMER_ABSTIME is set */
        timerfd_getTime(timerfd, old_value);
    }

    /* settings were modified, reset readability */
    legacyfile_adjustStatus(&(timerfd->super), STATUS_FILE_READABLE, FALSE);

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

        trace("Reading %" G_GUINT64_FORMAT " expirations from timer desc %p", expirationCount,
              &timerfd->super);

        *(guint64*)buf = expirationCount;

        /* reset readability */
        legacyfile_adjustStatus(&(timerfd->super), STATUS_FILE_READABLE, FALSE);

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
