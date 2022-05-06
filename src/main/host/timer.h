#ifndef MAIN_HOST_TIMER_H
#define MAIN_HOST_TIMER_H

#include "main/core/work/task.h"

typedef struct _Timer Timer;

Timer* timer_new(Task* task);
void timer_ref(Timer* timer);
void timer_unref(Timer* timer);
guint64 timer_consumeExpirationCount(Timer* timer);
guint64 timer_getExpirationCount(const Timer* timer);
EmulatedTime timer_getNextExpireTime(const Timer* timer);
SimulationTime timer_getInterval(const Timer* timer);
void timer_disarm(Timer* timer);
void timer_arm(Timer* timer, Host* host, EmulatedTime nextExpireTime,
               SimulationTime expireInterval);

#endif