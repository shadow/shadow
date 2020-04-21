/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_SCHEDULER_POLICY_H_
#define SHD_SCHEDULER_POLICY_H_

#include "main/core/work/event.h"
#include "main/host/host.h"

typedef enum {
    /* one global unlocked priority queue */
    SP_SERIAL_GLOBAL,
    /* every host has a locked pqueue into which every thread inserts events,
     * max queue contention is N for N threads */
    SP_PARALLEL_HOST_SINGLE,
    /* modified version of SP_PARALLEL_HOST_SINGLE that implements work stealing */
    SP_PARALLEL_HOST_STEAL,
    /* every thread has a locked pqueue into which every thread inserts events,
     * max queue contention is N for N threads */
    SP_PARALLEL_THREAD_SINGLE,
    /* every thread has a locked pqueue for every thread, each thread inserts into its one
     * assigned thread queue and max queue contention is 2 threads at any time */
    SP_PARALLEL_THREAD_PERTHREAD,
    /* every thread has a locked pqueue for every host, each thread inserts into its one
     * assigned host queue and max queue contention is 2 threads at any time */
    SP_PARALLEL_THREAD_PERHOST,
} SchedulerPolicyType;

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
