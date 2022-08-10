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

void schedulerpolicy_addHost(SchedulerPolicy* policy, Host* host, pthread_t assignedThread);
GQueue* schedulerpolicy_getAssignedHosts(SchedulerPolicy* policy);
void schedulerpolicy_free(SchedulerPolicy* policy);
SchedulerPolicy* schedulerpolicy_new();

#endif /* SHD_SCHEDULER_POLICY_H_ */
