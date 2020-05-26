/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/timer.h"

#include <bits/types/struct_itimerspec.h>
#include <bits/types/struct_timespec.h>
#include <bits/types/time_t.h>
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <time.h>

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/host.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _Timer {
    Descriptor super;

    /* the absolute time the timer will next expire */
    SimulationTime nextExpireTime;
    /* the relative periodic expiration interval */
    SimulationTime expireInterval;
    /* number of expires that happened since the timer was last set */
    guint64 expireCountSinceLastSet;

    /* expire ids are used internally to cancel events that fire after
     * they have become invalid because the user reset the timer */
    guint nextExpireID;
    guint minValidExpireID;

    guint numEventsScheduled;
    gboolean isClosed;

    MAGIC_DECLARE;
};

static void _timer_close(Timer* timer) {
    MAGIC_ASSERT(timer);
    debug("timer fd %i closing now", timer->super.handle);
    timer->isClosed = TRUE;
    descriptor_adjustStatus(&(timer->super), DS_ACTIVE, FALSE);
    if (timer->super.handle > 0) {
        host_closeDescriptor(worker_getActiveHost(), timer->super.handle);
    }
}

static void _timer_free(Timer* timer) {
    MAGIC_ASSERT(timer);
    MAGIC_CLEAR(timer);
    g_free(timer);
    worker_countObject(OBJECT_TYPE_TIMER, COUNTER_TYPE_FREE);
}

static DescriptorFunctionTable _timerFunctions = {
    (DescriptorFunc) _timer_close,
    (DescriptorFunc) _timer_free,
    MAGIC_VALUE
};

Timer* timer_new(gint handle, gint clockid, gint flags) {
    if(clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC) {
        errno = EINVAL;
        return NULL;
    }

    if(flags != 0 && flags != TFD_NONBLOCK && flags != TFD_CLOEXEC
            && flags != (TFD_NONBLOCK|TFD_CLOEXEC)) {
        errno = EINVAL;
        return NULL;
    }

//    if(!(flags&TFD_NONBLOCK)) {
//        warning("Shadow does not support blocking timers, using TFD_NONBLOCK flag implicitly");
//    }

    Timer* timer = g_new0(Timer, 1);
    MAGIC_INIT(timer);

    descriptor_init(&(timer->super), DT_TIMER, &_timerFunctions, handle);
    descriptor_adjustStatus(&(timer->super), DS_ACTIVE, TRUE);

    worker_countObject(OBJECT_TYPE_TIMER, COUNTER_TYPE_NEW);

    return timer;
}

static void _timer_getCurrentTime(Timer* timer, struct timespec* out) {
    MAGIC_ASSERT(timer);
    utility_assert(out);

    if(timer->nextExpireTime == 0) {
        /* timer is disarmed */
        out->tv_sec = 0;
        out->tv_nsec = 0;
    } else {
        /* timer is armed */
        SimulationTime currentTime = worker_getCurrentTime();
        utility_assert(currentTime <= timer->nextExpireTime);

        /* always return relative value */
        SimulationTime diff = timer->nextExpireTime - currentTime;
        out->tv_sec = (time_t) (diff / SIMTIME_ONE_SECOND);
        out->tv_nsec =(glong) (diff % SIMTIME_ONE_SECOND);
    }
}

static void _timer_getCurrentInterval(Timer* timer, struct timespec* out) {
    MAGIC_ASSERT(timer);
    utility_assert(out);

    if(timer->expireInterval == 0) {
        /* timer is set to expire just once */
        out->tv_sec = 0;
        out->tv_nsec = 0;
    } else {
        out->tv_sec = (time_t) (timer->expireInterval / SIMTIME_ONE_SECOND);
        out->tv_nsec =(glong) (timer->expireInterval % SIMTIME_ONE_SECOND);
    }
}

gint timer_getTime(Timer* timer, struct itimerspec *curr_value) {
    MAGIC_ASSERT(timer);

    if(!curr_value) {
        errno = EFAULT;
        return -1;
    }

    /* returns relative time */
    _timer_getCurrentTime(timer, &(curr_value->it_value));
    _timer_getCurrentInterval(timer, &(curr_value->it_interval));

    return 0;
}

static void _timer_disarm(Timer* timer) {
    MAGIC_ASSERT(timer);
    timer->nextExpireTime = 0;
    timer->expireInterval = 0;
    timer->minValidExpireID = timer->nextExpireID;
    debug("timer fd %i disarmed", timer->super.handle);
}

static SimulationTime _timer_timespecToSimTime(const struct timespec* config, gboolean configTimeIsEmulatedTime) {
    utility_assert(config);

    SimulationTime simNanoSecs = 0;

    if(configTimeIsEmulatedTime) {
        /* the time that was passed in represents an emulated time, so we need to adjust */
        EmulatedTime emNanoSecs = (EmulatedTime)(config->tv_sec * SIMTIME_ONE_SECOND);
        emNanoSecs += (EmulatedTime) config->tv_nsec;
        simNanoSecs = EMULATED_TIME_TO_SIMULATED_TIME(emNanoSecs);
    } else {
        /* the config is a relative time, so we just use simtime directly */
        simNanoSecs = (SimulationTime)(config->tv_sec * SIMTIME_ONE_SECOND);
        simNanoSecs += (SimulationTime) config->tv_nsec;
    }

    return simNanoSecs;
}

static void _timer_setCurrentTime(Timer* timer, const struct timespec* config, gint flags) {
    MAGIC_ASSERT(timer);
    utility_assert(config);

    SimulationTime now = worker_getCurrentTime();

    if(flags == TFD_TIMER_ABSTIME) {
        /* config time specifies an absolute time.
         * the plugin only knows about emulated time, so we need to convert it
         * back to simulated time to make sure we expire at the right time. */
        timer->nextExpireTime = _timer_timespecToSimTime(config, TRUE);

        /* the man page does not specify what happens if the time
         * they gave us is in the past. on linux, the result is an
         * immediate timer expiration. */
        if(timer->nextExpireTime < now) {
            timer->nextExpireTime = now;
        }
    } else {
        /* config time is relative to current time */
        timer->nextExpireTime = now + _timer_timespecToSimTime(config, FALSE);
    }
}

static void _timer_setCurrentInterval(Timer* timer, const struct timespec* config) {
    MAGIC_ASSERT(timer);
    utility_assert(config);

    /* config time for intervals is always just a raw number of seconds and nanos */
    timer->expireInterval = _timer_timespecToSimTime(config, FALSE);
}

static void _timer_expire(Timer* timer, gpointer data);

static void _timer_scheduleNewExpireEvent(Timer* timer) {
    MAGIC_ASSERT(timer);

    /* callback to our own node */
    gpointer next = GUINT_TO_POINTER(timer->nextExpireID);

    /* ref the timer storage in the callback event */
    descriptor_ref(timer);
    Task* task = task_new((TaskCallbackFunc)_timer_expire,
            timer, next, descriptor_unref, NULL);

    SimulationTime delay = timer->nextExpireTime - worker_getCurrentTime();

    /* if the user set a super long delay, let's call back sooner to check if they closed
     * or disarmed the timer in the meantime. This prevents queueing the task indefinitely. */
    delay = MIN(delay, SIMTIME_ONE_SECOND);

    worker_scheduleTask(task, delay);
    task_unref(task);

    timer->nextExpireID++;
    timer->numEventsScheduled++;
}

static void _timer_expire(Timer* timer, gpointer data) {
    MAGIC_ASSERT(timer);

    /* this is a task callback event */

    guint expireID = GPOINTER_TO_UINT(data);
    debug("timer fd %i expired; isClosed=%i expireID=%u minValidExpireID=%u",
          timer->super.handle, timer->isClosed, expireID,
          timer->minValidExpireID);

    timer->numEventsScheduled--;

    /* make sure the timer has not been reset since we scheduled this expiration event */
    if(!timer->isClosed && expireID >= timer->minValidExpireID) {
        /* check if it actually expired on this callback check */
        if(timer->nextExpireTime <= worker_getCurrentTime()) {
            /* if a one-time (non-periodic) timer already expired before they
             * started listening for the event with epoll, the event is reported
             * immediately on the next epoll_wait call. this behavior was
             * verified on linux. */
            timer->expireCountSinceLastSet++;
            descriptor_adjustStatus(&(timer->super), DS_READABLE, TRUE);

            if(timer->expireInterval > 0) {
                SimulationTime now = worker_getCurrentTime();
                timer->nextExpireTime += timer->expireInterval;
                if(timer->nextExpireTime < now) {
                    /* for some reason we looped the interval. expire again immediately
                     * to keep the periodic timer going. */
                    timer->nextExpireTime = now;
                }
                _timer_scheduleNewExpireEvent(timer);
            } else {
                /* the timer is now disarmed */
                _timer_disarm(timer);
            }
        } else {
            /* it didn't expire yet, check again in another second */
            _timer_scheduleNewExpireEvent(timer);
        }
    }
}

static void _timer_arm(Timer* timer, const struct itimerspec *config, gint flags) {
    MAGIC_ASSERT(timer);
    utility_assert(config);

    _timer_setCurrentTime(timer, &(config->it_value), flags);

    if(config->it_interval.tv_sec > 0 || config->it_interval.tv_nsec > 0) {
        _timer_setCurrentInterval(timer, &(config->it_interval));
    }

    SimulationTime now = worker_getCurrentTime();
    if(timer->nextExpireTime >= now) {
        _timer_scheduleNewExpireEvent(timer);
    }

    debug("timer fd %i armed to expire in %"G_GUINT64_FORMAT" nanos",
            timer->super.handle, timer->nextExpireTime - now);
}

static gboolean _timer_timeIsValid(const struct timespec* config) {
    utility_assert(config);
    return (config->tv_nsec < 0 || config->tv_nsec >= SIMTIME_ONE_SECOND) ? FALSE : TRUE;
}

gint timer_setTime(Timer* timer, gint flags,
                   const struct itimerspec *new_value,
                   struct itimerspec *old_value) {
    MAGIC_ASSERT(timer);

    if(!new_value) {
        errno = EFAULT;
        return -1;
    }

    if(!_timer_timeIsValid(&(new_value->it_value)) ||
            !_timer_timeIsValid(&(new_value->it_interval))) {
        errno = EINVAL;
        return -1;
    }

    if(flags != 0 && flags != TFD_TIMER_ABSTIME) {
        errno = EINVAL;
        return -1;
    }

    debug("Setting timer value to "
          "%" G_GUINT64_FORMAT ".%09" G_GUINT64_FORMAT " seconds "
          "and timer interval to "
          "%" G_GUINT64_FORMAT ".%09" G_GUINT64_FORMAT " seconds "
          "on timer fd %d",
          new_value->it_value.tv_sec, new_value->it_value.tv_nsec,
          new_value->it_interval.tv_sec, new_value->it_interval.tv_nsec,
          timer->super.handle);

    /* first get the old value if requested */
    if(old_value) {
        /* old value is always relative, even if TFD_TIMER_ABSTIME is set */
        timer_getTime(timer, old_value);
    }

    /* always disarm to invalidate old expire events */
    _timer_disarm(timer);

    /* now set the new times as requested */
    if(new_value->it_value.tv_sec > 0 || new_value->it_value.tv_nsec > 0) {
        /* the man page does not specify what to do if it_value says
         * to disarm the timer, but it_interval is a valid interval.
         * we verified on linux that intervals are only set when it_value
         * actually requests that we arm the timer, and ignored otherwise. */
        _timer_arm(timer, new_value, flags);
    }

    /* settings were modified, reset expire count and readability */
    timer->expireCountSinceLastSet = 0;
    descriptor_adjustStatus(&(timer->super), DS_READABLE, FALSE);

    return 0;
}

ssize_t timer_read(Timer* timer, void *buf, size_t count) {
    MAGIC_ASSERT(timer);

    if(timer->expireCountSinceLastSet > 0) {
        /* we have something to report, make sure the buf is big enough */
        if(count < sizeof(guint64)) {
            return (ssize_t)-EINVAL;
        }

        debug("Reading %" G_GUINT64_FORMAT " expirations from timer fd %d",
              timer->expireCountSinceLastSet, timer->super.handle);

        memcpy(buf, &(timer->expireCountSinceLastSet), sizeof(guint64));

        /* reset the expire count since we reported it */
        timer->expireCountSinceLastSet = 0;
        descriptor_adjustStatus(&(timer->super), DS_READABLE, FALSE);

        return (ssize_t) sizeof(guint64);
    } else {
        /* the timer has not yet expired, try again later */
        return (ssize_t)-EWOULDBLOCK;
    }
}

guint64 timer_getExpirationCount(Timer* timer) {
    MAGIC_ASSERT(timer);
    return timer->expireCountSinceLastSet;
}
