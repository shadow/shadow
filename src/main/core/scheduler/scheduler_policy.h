/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SHD_SCHEDULER_POLICY_H_
#define SHD_SCHEDULER_POLICY_H_

#include <glib.h>
#include <pthread.h>

#include "main/bindings/c/bindings-opaque.h"
#include "main/host/host.h"

typedef struct _SchedulerPolicy SchedulerPolicy;

void schedulerpolicy_addHost(SchedulerPolicy* policy, Host* host, pthread_t randomThread);
GQueue* schedulerpolicy_getAssignedHosts(SchedulerPolicy* policy);
void schedulerpolicy_push(SchedulerPolicy* policy, Event* event, Host* srcHost, Host* dstHost,
                          SimulationTime barrier);
Event* schedulerpolicy_pop(SchedulerPolicy* policy, SimulationTime barrier);
EmulatedTime schedulerpolicy_nextHostEventTime(SchedulerPolicy* policy, Host* host);
SimulationTime schedulerpolicy_getNextTime(SchedulerPolicy* policy);
void schedulerpolicy_free(SchedulerPolicy* policy);
SchedulerPolicy* schedulerpolicy_new();

#endif /* SHD_SCHEDULER_POLICY_H_ */
