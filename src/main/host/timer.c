#include "main/host/timer.h"

#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/utility/utility.h"

struct _Timer {
    /* the absolute time the timer will next expire */
    EmulatedTime nextExpireTime;
    /* the relative periodic expiration interval */
    SimulationTime expireInterval;
    /* Number of "undelivered" expirations.
     * Should be reset to 0 when the timer is reset, or when user-space
     * is notified (e.g. a timerfd is read).
     */
    guint64 undeliveredExpirationCount;

    /* expire ids are used internally to cancel events that fire after
     * they have become invalid because the user reset the timer */
    guint nextExpireID;
    guint minValidExpireID;

    Task* task;

    int referenceCount;

    MAGIC_DECLARE;
};

static void _timer_scheduleNewExpireEvent(Timer* timer, Host* host);
static void _timer_expire(Host* host, gpointer voidTimer, gpointer expireId);
static void _timer_unrefTaskObjectFreeFunc(gpointer timer) { timer_unref(timer); }

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
        worker_count_deallocation(Timer);
    }
}

Timer* timer_new(Task* task) {
    Timer* rv = g_new(Timer, 1);
    *rv = (Timer){.task = task,
                  .referenceCount = 1,
                  .nextExpireTime = EMUTIME_INVALID,
                  MAGIC_INITIALIZER};
    if (task) {
        task_ref(task);
    }
    worker_count_allocation(Timer);
    return rv;
}

void timer_resetUndeliveredExpirationCount(Timer* timer) {
    MAGIC_ASSERT(timer);
    timer->undeliveredExpirationCount = 0;
}

guint64 timer_getUndeliveredExpirationCount(const Timer* timer) {
    MAGIC_ASSERT(timer);
    return timer->undeliveredExpirationCount;
}

EmulatedTime timer_getNextExpireTime(const Timer* timer) {
    MAGIC_ASSERT(timer);

    return timer->nextExpireTime;
}

SimulationTime timer_getInterval(const Timer* timer) {
    MAGIC_ASSERT(timer);

    return timer->expireInterval;
}

static void _timer_cancelScheduledExpirationEvents(Timer* timer) {
    MAGIC_ASSERT(timer);
    timer->minValidExpireID = timer->nextExpireID;
}

void timer_disarm(Timer* timer) {
    MAGIC_ASSERT(timer);
    timer->nextExpireTime = EMUTIME_INVALID;
    timer->expireInterval = 0;

    _timer_cancelScheduledExpirationEvents(timer);
}

void timer_arm(Timer* timer, Host* host, EmulatedTime nextExpireTime,
               SimulationTime expireInterval) {
    MAGIC_ASSERT(timer);

    _timer_cancelScheduledExpirationEvents(timer);

    utility_assert(nextExpireTime != EMUTIME_INVALID);
    utility_assert(nextExpireTime >= worker_getCurrentEmulatedTime());
    timer->nextExpireTime = nextExpireTime;

    utility_assert(expireInterval != SIMTIME_INVALID);
    timer->expireInterval = expireInterval;

    _timer_scheduleNewExpireEvent(timer, host);
}

static void _timer_scheduleNewExpireEvent(Timer* timer, Host* host) {
    MAGIC_ASSERT(timer);

    /* callback to our own node */
    gpointer next = GUINT_TO_POINTER(timer->nextExpireID);

    /* ref the timer storage in the callback event */
    timer_ref(timer);
    Task* task = task_new(_timer_expire, timer, next, _timer_unrefTaskObjectFreeFunc, NULL);

    SimulationTime delay = timer->nextExpireTime - worker_getCurrentEmulatedTime();

    /* if the user set a super long delay, let's call back sooner to check if they closed
     * or disarmed the timer in the meantime. This prevents queueing the task indefinitely. */
    delay = MIN(delay, SIMTIME_ONE_SECOND);

    trace("Scheduling timer expiration task for %" G_GUINT64_FORMAT " nanoseconds", delay);
    worker_scheduleTaskWithDelay(task, host, delay);
    task_unref(task);

    timer->nextExpireID++;
}

static void _timer_expire(Host* host, gpointer voidTimer, gpointer voidExpireId) {
    Timer* timer = voidTimer;
    MAGIC_ASSERT(timer);

    /* this is a task callback event */

    guint expireID = GPOINTER_TO_UINT(voidExpireId);
    trace("timer expire check; expireID=%u minValidExpireID=%u", expireID, timer->minValidExpireID);

    /* make sure the timer has not been reset since we scheduled this expiration event */
    if (expireID < timer->minValidExpireID) {
        return;
    }

    utility_assert(timer->nextExpireTime != EMUTIME_INVALID);

    if (timer->nextExpireTime > worker_getCurrentEmulatedTime()) {
        /* it didn't expire yet, check again in another second */
        _timer_scheduleNewExpireEvent(timer, host);
        return;
    }

    /* check if it actually expired on this callback check. */
    if (timer->nextExpireTime <= worker_getCurrentEmulatedTime()) {
        ++timer->undeliveredExpirationCount;
        if (timer->task) {
            task_execute(timer->task, host);
        }

        if (timer->expireInterval > 0) {
            EmulatedTime now = worker_getCurrentEmulatedTime();
            timer->nextExpireTime += timer->expireInterval;
            if (timer->nextExpireTime < now) {
                /* for some reason we looped the interval. expire again immediately
                 * to keep the periodic timer going. */
                timer->nextExpireTime = now;
            }
            _timer_scheduleNewExpireEvent(timer, host);
        } else {
            /* the timer is now disarmed */
            timer_disarm(timer);
        }
    }
}
