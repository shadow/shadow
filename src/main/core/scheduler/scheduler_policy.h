/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_SCHEDULER_POLICY_H_
#define SHD_SCHEDULER_POLICY_H_

typedef struct _SchedulerPolicy SchedulerPolicy;

#include "main/core/work/event.h"
#include "main/host/host.h"
#include "main/utility/utility.h"

typedef void (*SchedulerPolicyAddHostFunc)(SchedulerPolicy*, Host*, pthread_t);
typedef GQueue* (*SchedulerPolicyGetHostsFunc)(SchedulerPolicy*);
typedef void (*SchedulerPolicyPushFunc)(SchedulerPolicy*, Event*, Host*, Host*, SimulationTime);
typedef Event* (*SchedulerPolicyPopFunc)(SchedulerPolicy*, SimulationTime);
typedef EmulatedTime (*SchedulerPolicyNextHostEventTimeFunc)(SchedulerPolicy*, Host*);
typedef SimulationTime (*SchedulerPolicyGetNextTimeFunc)(SchedulerPolicy*);
typedef void (*SchedulerPolicyFreeFunc)(SchedulerPolicy*);

struct _SchedulerPolicy {
    gpointer data;
    SchedulerPolicyAddHostFunc addHost;
    SchedulerPolicyGetHostsFunc getAssignedHosts;
    SchedulerPolicyPushFunc push;
    SchedulerPolicyPopFunc pop;
    SchedulerPolicyNextHostEventTimeFunc nextHostEventTime;
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
