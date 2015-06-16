/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef SHD_SCHEDULER_H_
#define SHD_SCHEDULER_H_

#include "shadow.h"

typedef struct _Scheduler Scheduler;

Scheduler* scheduler_new(SchedulerPolicyType policyType, guint nWorkers, gpointer threadUserData,
        guint schedulerSeed, SimulationTime endTime);
void scheduler_ref(Scheduler*);
void scheduler_unref(Scheduler*);

void scheduler_awaitStart(Scheduler*);
void scheduler_awaitFinish(Scheduler*);
void scheduler_start(Scheduler*);
void scheduler_continueNextRound(Scheduler*, SimulationTime, SimulationTime);
SimulationTime scheduler_awaitNextRound(Scheduler*);
void scheduler_finish(Scheduler*);

void scheduler_push(Scheduler*, Event*, GQuark, GQuark);
Event* scheduler_pop(Scheduler*);

void scheduler_addHost(Scheduler*, Host*);
Host* scheduler_getHost(Scheduler*, GQuark);
SchedulerPolicyType scheduler_getPolicy(Scheduler*);
gboolean scheduler_isRunning(Scheduler* scheduler);

#endif /* SHD_SCHEDULER_H_ */
