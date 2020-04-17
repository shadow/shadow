/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef struct _GlobalSinglePolicyData GlobalSinglePolicyData;
struct _GlobalSinglePolicyData {
    PriorityQueue* pq;
    SimulationTime lastEventTime;
    gsize nPushed;
    gsize nPopped;
    GQueue* assignedHosts;
    MAGIC_DECLARE;
};

static void _schedulerpolicyglobalsingle_addHost(SchedulerPolicy* policy, Host* host, pthread_t randomThread) {
    MAGIC_ASSERT(policy);
    /* we dont need to store any special mappings because we only have a single pqueue */
    GlobalSinglePolicyData* data = policy->data;
    g_queue_push_tail(data->assignedHosts, host);
}

static GQueue* _schedulerpolicyglobalsingle_getHosts(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    GlobalSinglePolicyData* data = policy->data;
    return data->assignedHosts;
}

static void _schedulerpolicyglobalsingle_push(SchedulerPolicy* policy, Event* event, Host* srcHost, Host* dstHost, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    GlobalSinglePolicyData* data = policy->data;
    priorityqueue_push(data->pq, event);
}

static gboolean _hostsProcessesAreFinished(GlobalSinglePolicyData* policyData) {
    if (g_queue_is_empty(policyData->assignedHosts))
        return TRUE;

    gboolean hosts_are_finished = TRUE;
    GQueue *hosts = g_queue_copy(policyData->assignedHosts);

    while (!g_queue_is_empty(hosts)) {
        Host *h = g_queue_pop_head(hosts);

        if (host_processesAreFinished(h)) {
            hosts_are_finished = FALSE;
            break;
        }
    }

    g_queue_free(hosts);
    return hosts_are_finished;
}

static Event* _schedulerpolicyglobalsingle_pop(SchedulerPolicy* policy, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    GlobalSinglePolicyData* data = policy->data;

    Event* nextEvent = priorityqueue_peek(data->pq);
    if(!nextEvent || _hostsProcessesAreFinished(data)) {
        return NULL;
    }

    SimulationTime eventTime = event_getTime(nextEvent);
    if(eventTime >= barrier) {
        return NULL;
    }

    utility_assert(eventTime >= data->lastEventTime);
    data->lastEventTime = eventTime;

    return priorityqueue_pop(data->pq);
}

static SimulationTime _schedulerpolicyglobalsingle_getNextTime(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    GlobalSinglePolicyData* data = policy->data;
    Event* nextEvent = priorityqueue_peek(data->pq);
    return (nextEvent != NULL) ? event_getTime(nextEvent) : SIMTIME_MAX;
}

static void _schedulerpolicyglobalsingle_free(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    GlobalSinglePolicyData* data = policy->data;

    if(data->pq) {
        priorityqueue_free(data->pq);
    }
    if(data->assignedHosts) {
        g_queue_free(data->assignedHosts);
    }
    g_free(data);

    MAGIC_CLEAR(policy);
    g_free(policy);
}

SchedulerPolicy* schedulerpolicyglobalsingle_new() {
    GlobalSinglePolicyData* data = g_new0(GlobalSinglePolicyData, 1);
    data->pq = priorityqueue_new((GCompareDataFunc)event_compare, NULL, (GDestroyNotify)event_unref);
    data->assignedHosts = g_queue_new();

    SchedulerPolicy* policy = g_new0(SchedulerPolicy, 1);
    MAGIC_INIT(policy);
    policy->addHost = _schedulerpolicyglobalsingle_addHost;
    policy->getAssignedHosts = _schedulerpolicyglobalsingle_getHosts;
    policy->push = _schedulerpolicyglobalsingle_push;
    policy->pop = _schedulerpolicyglobalsingle_pop;
    policy->getNextTime = _schedulerpolicyglobalsingle_getNextTime;
    policy->free = _schedulerpolicyglobalsingle_free;

    policy->type = SP_SERIAL_GLOBAL;
    policy->data = data;
    policy->referenceCount = 1;

    return policy;
}

