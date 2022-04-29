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
#include "main/utility/utility.h"

typedef struct _Timer Timer;
struct _Timer {
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

    Task* task;

    int referenceCount;

    MAGIC_DECLARE;
};

void timer_ref(Timer* timer) {
    MAGIC_ASSERT(timer);

    utility_assert(timer->referenceCount > 0);
    ++timer->referenceCount;
}

void timer_unref(Timer* timer) {
    MAGIC_ASSERT(timer);

    utility_assert(timer->referenceCount > 0);
    if (--timer->referenceCount == 0) {
        if (timer->task) {
            task_unref(timer->task);
            timer->task = NULL;
        }
        MAGIC_CLEAR(timer);
        g_free(timer);
    }
}

Timer* timer_new(Task* task) {
    Timer* rv = g_new(Timer, 1);
    *rv = (Timer){.task = task,
                  .referenceCount = 1,

                  MAGIC_INITIALIZER};
    if (task) {
        task_ref(task);
    }
    return rv;
}

static void _timer_unrefTaskObjectFreeFunc(gpointer timer) { timer_unref(timer); }

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

static void _timer_getCurrentTime(const Timer* timer, struct timespec* out) {
    MAGIC_ASSERT(timer);
    utility_assert(out);

    if (timer->nextExpireTime == 0) {
        /* timer is disarmed */
        out->tv_sec = 0;
        out->tv_nsec = 0;
    } else {
        /* timer is armed */
        SimulationTime currentTime = worker_getCurrentSimulationTime();
        utility_assert(currentTime <= timer->nextExpireTime);

        /* always return relative value */
        SimulationTime diff = timer->nextExpireTime - currentTime;
        out->tv_sec = (time_t) (diff / SIMTIME_ONE_SECOND);
        out->tv_nsec =(glong) (diff % SIMTIME_ONE_SECOND);
    }
}

static void _timer_getCurrentInterval(const Timer* timer, struct timespec* out) {
    MAGIC_ASSERT(timer);
    utility_assert(out);

    if (timer->expireInterval == 0) {
        /* timer is set to expire just once */
        out->tv_sec = 0;
        out->tv_nsec = 0;
    } else {
        out->tv_sec = (time_t)(timer->expireInterval / SIMTIME_ONE_SECOND);
        out->tv_nsec = (glong)(timer->expireInterval % SIMTIME_ONE_SECOND);
    }
}

void timerfd_getTime(const TimerFd* timerfd, struct itimerspec* curr_value) {
    MAGIC_ASSERT(timerfd);
    utility_assert(curr_value);

    /* returns relative time */
    _timer_getCurrentTime(timerfd->timer, &(curr_value->it_value));
    _timer_getCurrentInterval(timerfd->timer, &(curr_value->it_interval));
}

static void _timer_disarm(Timer* timer) {
    MAGIC_ASSERT(timer);
    timer->nextExpireTime = 0;
    timer->expireInterval = 0;
    timer->minValidExpireID = timer->nextExpireID;
}

static void _timerfd_disarm(TimerFd* timerfd) {
    MAGIC_ASSERT(timerfd);
    _timer_disarm(timerfd->timer);
    trace("timer fd %i disarmed", timerfd->super.handle);
}

static SimulationTime _timerfd_timespecToSimTime(const struct timespec* config,
                                                 gboolean configTimeIsEmulatedTime) {
    utility_assert(config);

    SimulationTime simNanoSecs = 0;

    if(configTimeIsEmulatedTime) {
        /* the time that was passed in represents an emulated time, so we need to adjust */
        EmulatedTime emNanoSecs = (EmulatedTime)(config->tv_sec * SIMTIME_ONE_SECOND);
        emNanoSecs += (EmulatedTime) config->tv_nsec;
        /* If the emulated time passed in by the plugin is before the time we use as the
         * start of the simulation (i.e., EMULATED_TIME_OFFSET), then we use t=0 as 
         * a proxy for "some time in the past". */
        if(emNanoSecs >= EMULATED_TIME_OFFSET) {
            simNanoSecs = EMULATED_TIME_TO_SIMULATED_TIME(emNanoSecs);
        } else {
            simNanoSecs = 0;
        }
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

    SimulationTime now = worker_getCurrentSimulationTime();

    if(flags == TFD_TIMER_ABSTIME) {
        /* config time specifies an absolute time.
         * the plugin only knows about emulated time, so we need to convert it
         * back to simulated time to make sure we expire at the right time. */
        timer->nextExpireTime = _timerfd_timespecToSimTime(config, TRUE);

        /* the man page does not specify what happens if the time
         * they gave us is in the past. on linux, the result is an
         * immediate timer expiration. */
        if (timer->nextExpireTime < now) {
            timer->nextExpireTime = now;
        }
    } else {
        /* config time is relative to current time */
        timer->nextExpireTime = now + _timerfd_timespecToSimTime(config, FALSE);
    }
}

static void _timer_setCurrentInterval(Timer* timer, const struct timespec* config) {
    MAGIC_ASSERT(timer);
    utility_assert(config);

    /* config time for intervals is always just a raw number of seconds and nanos */
    timer->expireInterval = _timerfd_timespecToSimTime(config, FALSE);
}

static void _timer_expire(Host* host, gpointer voidTimer, gpointer expireId);

static void _timer_scheduleNewExpireEvent(Timer* timer, Host* host) {
    MAGIC_ASSERT(timer);

    /* callback to our own node */
    gpointer next = GUINT_TO_POINTER(timer->nextExpireID);

    /* ref the timer storage in the callback event */
    timer_ref(timer);
    Task* task = task_new(_timer_expire, timer, next, _timer_unrefTaskObjectFreeFunc, NULL);

    SimulationTime delay = timer->nextExpireTime - worker_getCurrentSimulationTime();

    /* if the user set a super long delay, let's call back sooner to check if they closed
     * or disarmed the timer in the meantime. This prevents queueing the task indefinitely. */
    delay = MIN(delay, SIMTIME_ONE_SECOND);

    trace("Scheduling timer expiration task for %"G_GUINT64_FORMAT" nanoseconds", delay);
    worker_scheduleTaskWithDelay(task, host, delay);
    task_unref(task);

    timer->nextExpireID++;
    timer->numEventsScheduled++;
}

static void _timerfd_expire(Host* host, gpointer voidTimerFd, gpointer data) {
    TimerFd* timerfd = voidTimerFd;
    MAGIC_ASSERT(timerfd);

    if (!timerfd->isClosed) {
        descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_READABLE, TRUE);
    }
}

static void _timer_expire(Host* host, gpointer voidTimer, gpointer voidExpireId) {
    Timer* timer = voidTimer;
    MAGIC_ASSERT(timer);

    /* this is a task callback event */

    guint expireID = GPOINTER_TO_UINT(voidExpireId);
    trace("timer expire check; expireID=%u minValidExpireID=%u", expireID, timer->minValidExpireID);

    timer->numEventsScheduled--;

    /* make sure the timer has not been reset since we scheduled this expiration event */
    if (expireID >= timer->minValidExpireID) {
        /* check if it actually expired on this callback check */
        if (timer->nextExpireTime <= worker_getCurrentSimulationTime()) {
            /* if a one-time (non-periodic) timer already expired before they
             * started listening for the event with epoll, the event is reported
             * immediately on the next epoll_wait call. this behavior was
             * verified on linux. */
            timer->expireCountSinceLastSet++;
            if (timer->task) {
                task_execute(timer->task, host);
            }

            if (timer->expireInterval > 0) {
                SimulationTime now = worker_getCurrentSimulationTime();
                timer->nextExpireTime += timer->expireInterval;
                if (timer->nextExpireTime < now) {
                    /* for some reason we looped the interval. expire again immediately
                     * to keep the periodic timer going. */
                    timer->nextExpireTime = now;
                }
                _timer_scheduleNewExpireEvent(timer, host);
            } else {
                /* the timer is now disarmed */
                _timer_disarm(timer);
            }
        } else {
            /* it didn't expire yet, check again in another second */
            _timer_scheduleNewExpireEvent(timer, host);
        }
    }
}

static void _timerfd_arm(TimerFd* timerfd, Host* host, const struct itimerspec* config,
                         gint flags) {
    MAGIC_ASSERT(timerfd);
    utility_assert(config);

    _timer_setCurrentTime(timerfd->timer, &(config->it_value), flags);

    if(config->it_interval.tv_sec > 0 || config->it_interval.tv_nsec > 0) {
        _timer_setCurrentInterval(timerfd->timer, &(config->it_interval));
    }

    SimulationTime now = worker_getCurrentSimulationTime();
    if (timerfd->timer->nextExpireTime >= now) {
        _timer_scheduleNewExpireEvent(timerfd->timer, host);
        trace("timer fd %i armed to expire in %" G_GUINT64_FORMAT " nanos", timerfd->super.handle,
              timerfd->timer->nextExpireTime - now);
    }
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

    /* always disarm to invalidate old expire events */
    _timerfd_disarm(timerfd);

    /* settings were modified, reset expire count and readability */
    timerfd->timer->expireCountSinceLastSet = 0;
    descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_READABLE, FALSE);

    /* now set the new times as requested */
    if(new_value->it_value.tv_sec > 0 || new_value->it_value.tv_nsec > 0) {
        /* the man page does not specify what to do if it_value says
         * to disarm the timer, but it_interval is a valid interval.
         * we verified on linux that intervals are only set when it_value
         * actually requests that we arm the timer, and ignored otherwise. */
        _timerfd_arm(timerfd, host, new_value, flags);
    }

    return 0;
}

ssize_t timerfd_read(TimerFd* timerfd, void* buf, size_t count) {
    MAGIC_ASSERT(timerfd);

    if (timerfd->timer->expireCountSinceLastSet > 0) {
        /* we have something to report, make sure the buf is big enough */
        if(count < sizeof(guint64)) {
            return (ssize_t)-EINVAL;
        }

        trace("Reading %" G_GUINT64_FORMAT " expirations from timer fd %d",
              timerfd->timer->expireCountSinceLastSet, timerfd->super.handle);

        memcpy(buf, &(timerfd->timer->expireCountSinceLastSet), sizeof(guint64));

        /* reset the expire count since we reported it */
        timerfd->timer->expireCountSinceLastSet = 0;
        descriptor_adjustStatus(&(timerfd->super), STATUS_DESCRIPTOR_READABLE, FALSE);

        return (ssize_t) sizeof(guint64);
    } else {
        /* the timer has not yet expired, try again later */
        return (ssize_t)-EWOULDBLOCK;
    }
}

guint64 timerfd_getExpirationCount(const TimerFd* timerfd) {
    MAGIC_ASSERT(timerfd);
    return timerfd->timer->expireCountSinceLastSet;
}
