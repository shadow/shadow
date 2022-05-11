#ifndef MAIN_HOST_TIMER_H
#define MAIN_HOST_TIMER_H

typedef struct _Timer Timer;

#include "main/bindings/c/bindings.h"

Timer* timer_new(TaskRef* task);
void timer_ref(Timer* timer);
void timer_unref(Timer* timer);

// Get the current expiration count.
guint64 timer_getExpirationCount(const Timer* timer);

// Get and reset the current expiration count.
guint64 timer_consumeExpirationCount(Timer* timer);

// Get time until next expiration. Returns 0 if the timer is disarmed.
SimulationTime timer_getRemainingTime(const Timer* timer);

SimulationTime timer_getInterval(const Timer* timer);
void timer_disarm(Timer* timer);
void timer_arm(Timer* timer, Host* host, EmulatedTime nextExpireTime,
               SimulationTime expireInterval);

#endif