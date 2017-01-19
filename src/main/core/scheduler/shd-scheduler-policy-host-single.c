/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef struct _HostSingleQueueData HostSingleQueueData;
struct _HostSingleQueueData {
    GMutex lock;
    PriorityQueue* pq;
    SimulationTime pushSequenceCounter;
    SimulationTime lastEventTime;
    gsize nPushed;
    gsize nPopped;
};

typedef struct _HostSingleThreadData HostSingleThreadData;
struct _HostSingleThreadData {
    GList* assignedHosts;
    GList* currentItem;
    SimulationTime currentBarrier;
};

typedef struct _HostSinglePolicyData HostSinglePolicyData;
struct _HostSinglePolicyData {
    GHashTable* hostToQueueDataMap;
    GHashTable* threadToThreadDataMap;
    GHashTable* hostToThreadMap;
    MAGIC_DECLARE;
};

static HostSingleThreadData* _hostsinglethreaddata_new() {
    return g_new0(HostSingleThreadData, 1);
}

static void _hostsinglethreaddata_free(HostSingleThreadData* tdata) {
    if(tdata) {
        if(tdata->assignedHosts) {
            g_list_free(tdata->assignedHosts);
        }
        g_free(tdata);
    }
}

static HostSingleQueueData* _hostsinglequeuedata_new() {
    HostSingleQueueData* qdata = g_new0(HostSingleQueueData, 1);

    g_mutex_init(&(qdata->lock));
    qdata->pq = priorityqueue_new((GCompareDataFunc)event_compare, NULL, (GDestroyNotify)event_unref);

    return qdata;
}

static void _hostsinglequeuedata_free(HostSingleQueueData* qdata) {
    if(qdata) {
        if(qdata->pq) {
            priorityqueue_free(qdata->pq);
        }
        g_mutex_clear(&(qdata->lock));
        g_free(qdata);
    }
}

/* this must be run synchronously, or the call must be protected by locks */
static void _schedulerpolicyhostsingle_addHost(SchedulerPolicy* policy, Host* host, GThread* randomThread) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    /* each host has its own queue */
    if(!g_hash_table_lookup(data->hostToQueueDataMap, host)) {
        g_hash_table_replace(data->hostToQueueDataMap, host, _hostsinglequeuedata_new());
    }

    /* each thread keeps track of the hosts it needs to run */
    GThread* assignedThread = (randomThread != NULL) ? randomThread : g_thread_self();
    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, assignedThread);
    if(!tdata) {
        tdata = _hostsinglethreaddata_new();
        g_hash_table_replace(data->threadToThreadDataMap, assignedThread, tdata);
    }
    tdata->assignedHosts = g_list_append(tdata->assignedHosts, host);

    /* finally, store the host-to-thread mapping */
    g_hash_table_replace(data->hostToThreadMap, host, assignedThread);
}

static GList* _schedulerpolicyhostsingle_getHosts(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;
    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, g_thread_self());
    return (tdata != NULL) ? tdata->assignedHosts : NULL;
}

static void _schedulerpolicyhostsingle_push(SchedulerPolicy* policy, Event* event, Host* srcHost, Host* dstHost, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    /* non-local events must be properly delayed so the event wont show up at another host
     * before the next scheduling interval. if the thread scheduler guaranteed to always run
     * the minimum time event accross all of its assigned hosts, then we would only need to
     * do the time adjustment if the srcThread and dstThread are not identical. however,
     * the logic of this policy allows a thread to run all events from a given host before
     * moving on to the next host, so we must adjust the time whenever the srcHost and
     * dstHost are not the same. */
    SimulationTime eventTime = event_getTime(event);

    if(srcHost != dstHost && eventTime < barrier) {
        event_setTime(event, barrier);
        info("Inter-host event time %"G_GUINT64_FORMAT" changed to %"G_GUINT64_FORMAT" "
                "to ensure event causality", eventTime, barrier);
    }

    /* get the queue for the destination */
    HostSingleQueueData* qdata = g_hash_table_lookup(data->hostToQueueDataMap, dstHost);
    utility_assert(qdata);

    /* 'deliver' the event there */
    g_mutex_lock(&(qdata->lock));
    event_setSequence(event, ++(qdata->pushSequenceCounter));
    priorityqueue_push(qdata->pq, event);
    qdata->nPushed++;
    g_mutex_unlock(&(qdata->lock));
}

static Event* _schedulerpolicyhostsingle_pop(SchedulerPolicy* policy, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    /* figure out which hosts we should be checking */
    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, g_thread_self());
    /* if there is no tdata, that means this thread didn't get any hosts assigned to it */
    if(!tdata) {
        /* this thread will remain idle */
        return NULL;
    }

    if(barrier > tdata->currentBarrier) {
        tdata->currentBarrier = barrier;
        tdata->currentItem = tdata->assignedHosts;
    }

    while(tdata->currentItem) {
        Host* host = tdata->currentItem->data;
        HostSingleQueueData* qdata = g_hash_table_lookup(data->hostToQueueDataMap, host);
        utility_assert(qdata);

        g_mutex_lock(&(qdata->lock));

        Event* nextEvent = priorityqueue_peek(qdata->pq);
        SimulationTime eventTime = (nextEvent != NULL) ? event_getTime(nextEvent) : SIMTIME_INVALID;

        if(nextEvent != NULL && eventTime < barrier) {
            utility_assert(eventTime >= qdata->lastEventTime);
            qdata->lastEventTime = eventTime;
            nextEvent = priorityqueue_pop(qdata->pq);
            qdata->nPopped++;
        } else {
            nextEvent = NULL;
        }

        g_mutex_unlock(&(qdata->lock));

        if(nextEvent != NULL) {
            return nextEvent;
        }
        /* try the next host if we still have more */
        tdata->currentItem = g_list_next(tdata->currentItem);
    }

    /* if we make it here, all hosts for this thread have no more events before barrier */
    return NULL;
}

static SimulationTime _schedulerpolicyhostsingle_getNextTime(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    SimulationTime nextTime = SIMTIME_MAX;

    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, g_thread_self());
    if(tdata) {
        GList* item = tdata->assignedHosts;
        while(item) {
            Host* host = item->data;
            HostSingleQueueData* qdata = g_hash_table_lookup(data->hostToQueueDataMap, host);
            utility_assert(qdata);
            g_mutex_lock(&(qdata->lock));
            Event* event = priorityqueue_peek(qdata->pq);
            g_mutex_unlock(&(qdata->lock));
            if(event != NULL) {
                nextTime = MIN(nextTime, event_getTime(event));
            }
            item = g_list_next(item);
        }
    }
    info("next event at time %"G_GUINT64_FORMAT, nextTime);

    return nextTime;
}

static void _schedulerpolicyhostsingle_free(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    g_hash_table_destroy(data->hostToQueueDataMap);
    g_hash_table_destroy(data->threadToThreadDataMap);
    g_hash_table_destroy(data->hostToThreadMap);
    g_free(data);

    MAGIC_CLEAR(policy);
    g_free(policy);
}

SchedulerPolicy* schedulerpolicyhostsingle_new() {
    HostSinglePolicyData* data = g_new0(HostSinglePolicyData, 1);
    data->hostToQueueDataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_hostsinglequeuedata_free);
    data->threadToThreadDataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_hostsinglethreaddata_free);
    data->hostToThreadMap = g_hash_table_new(g_direct_hash, g_direct_equal);

    SchedulerPolicy* policy = g_new0(SchedulerPolicy, 1);
    MAGIC_INIT(policy);
    policy->addHost = _schedulerpolicyhostsingle_addHost;
    policy->getAssignedHosts = _schedulerpolicyhostsingle_getHosts;
    policy->push = _schedulerpolicyhostsingle_push;
    policy->pop = _schedulerpolicyhostsingle_pop;
    policy->getNextTime = _schedulerpolicyhostsingle_getNextTime;
    policy->free = _schedulerpolicyhostsingle_free;

    policy->type = SP_PARALLEL_HOST_SINGLE;
    policy->data = data;
    policy->referenceCount = 1;

    return policy;
}

