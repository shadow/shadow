/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_SCHEDULER_H_
#define SHD_SCHEDULER_H_

#include <glib.h>

typedef struct _Scheduler Scheduler;

#include "main/core/scheduler/scheduler_policy.h"
#include "main/core/support/definitions.h"
#include "main/host/host.h"

Scheduler* scheduler_new(const Controller* controller, const ChildPidWatcher* pidWatcher,
                         const ConfigOptions* config, guint nWorkers, guint schedulerSeed,
                         SimulationTime endTime);
void scheduler_free(Scheduler*);
void scheduler_shutdown(Scheduler* scheduler);

void scheduler_awaitStart(Scheduler*);
void scheduler_awaitFinish(Scheduler*);
void scheduler_start(Scheduler*);
void scheduler_continueNextRound(Scheduler*, SimulationTime, SimulationTime);
SimulationTime scheduler_awaitNextRound(Scheduler*);
void scheduler_finish(Scheduler*);

gboolean scheduler_push(Scheduler*, Event*, Host* sender, Host* receiver);
Event* scheduler_pop(Scheduler*);
// Scheduled time of next event for `host`, or 0 if there is none.
EmulatedTime scheduler_nextHostEventTime(Scheduler*, Host* host);

int scheduler_addHost(Scheduler*, Host*);
const ThreadSafeEventQueue* scheduler_getEventQueue(Scheduler* scheduler, HostId host);
gboolean scheduler_isRunning(Scheduler* scheduler);

#endif /* SHD_SCHEDULER_H_ */
