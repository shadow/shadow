/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_SCHEDULER_POLICY_H_
#define SHD_SCHEDULER_POLICY_H_

#include "main/core/scheduler/scheduler_policy_type.h"
#include "main/core/work/event.h"
#include "main/host/host.h"

typedef struct _SchedulerPolicy SchedulerPolicy;

typedef void (*SchedulerPolicyAddHostFunc)(SchedulerPolicy*, Host*, pthread_t);
typedef GQueue* (*SchedulerPolicyGetHostsFunc)(SchedulerPolicy*);
typedef void (*SchedulerPolicyPushFunc)(SchedulerPolicy*, Event*, Host*, Host*, SimulationTime);
typedef Event* (*SchedulerPolicyPopFunc)(SchedulerPolicy*, SimulationTime);
typedef SimulationTime (*SchedulerPolicyGetNextTimeFunc)(SchedulerPolicy*);
typedef void (*SchedulerPolicyFreeFunc)(SchedulerPolicy*);

struct _SchedulerPolicy {
    SchedulerPolicyType type;
    gpointer data;
    gint referenceCount;
    SchedulerPolicyAddHostFunc addHost;
    SchedulerPolicyGetHostsFunc getAssignedHosts;
    SchedulerPolicyPushFunc push;
    SchedulerPolicyPopFunc pop;
    SchedulerPolicyGetNextTimeFunc getNextTime;
    SchedulerPolicyFreeFunc free;
    MAGIC_DECLARE;
};

SchedulerPolicy* schedulerpolicyglobalsingle_new();
SchedulerPolicy* schedulerpolicyhostsingle_new();
SchedulerPolicy* schedulerpolicyhoststeal_new();
SchedulerPolicy* schedulerpolicythreadsingle_new();
SchedulerPolicy* schedulerpolicythreadperthread_new();
SchedulerPolicy* schedulerpolicythreadperhost_new();

#endif /* SHD_SCHEDULER_POLICY_H_ */
