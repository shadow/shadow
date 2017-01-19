/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef struct _ThreadSingleThreadData ThreadSingleThreadData;
struct _ThreadSingleThreadData {
    GList* assignedHosts;
    GMutex lock;
    PriorityQueue* pq;
    SimulationTime pushSequenceCounter;
    SimulationTime lastEventTime;
    gsize nPushed;
    gsize nPopped;
};

typedef struct _ThreadSinglePolicyData ThreadSinglePolicyData;
struct _ThreadSinglePolicyData {
    GHashTable* threadToThreadDataMap;
    GHashTable* hostToThreadMap;
    MAGIC_DECLARE;
};

static ThreadSingleThreadData* _threadsinglethreaddata_new() {
    ThreadSingleThreadData* tdata = g_new0(ThreadSingleThreadData, 1);
    g_mutex_init(&(tdata->lock));
    tdata->pq = priorityqueue_new((GCompareDataFunc)event_compare, NULL, (GDestroyNotify)event_unref);
    return tdata;
}

static void _threadsinglethreaddata_free(ThreadSingleThreadData* tdata) {
    if(tdata) {
        if(tdata->assignedHosts) {
            g_list_free(tdata->assignedHosts);
        }
        if(tdata->pq) {
            priorityqueue_free(tdata->pq);
        }
        g_mutex_clear(&(tdata->lock));
        g_free(tdata);
    }
}

/* this must be run synchronously, or the call must be protected by locks */
static void _schedulerpolicythreadsingle_addHost(SchedulerPolicy* policy, Host* host, GThread* randomThread) {
    MAGIC_ASSERT(policy);
    ThreadSinglePolicyData* data = policy->data;

    /* each thread keeps track of the hosts it needs to run */
    GThread* assignedThread = (randomThread != NULL) ? randomThread : g_thread_self();
    ThreadSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, assignedThread);
    if(!tdata) {
        tdata = _threadsinglethreaddata_new();
        g_hash_table_replace(data->threadToThreadDataMap, assignedThread, tdata);
    }
    tdata->assignedHosts = g_list_append(tdata->assignedHosts, host);

    /* finally, store the host-to-thread mapping */
    g_hash_table_replace(data->hostToThreadMap, host, assignedThread);
}

static GList* _schedulerpolicythreadsingle_getHosts(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    ThreadSinglePolicyData* data = policy->data;
    ThreadSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, g_thread_self());
    return (tdata != NULL) ? tdata->assignedHosts : NULL;
}

static void _schedulerpolicythreadsingle_push(SchedulerPolicy* policy, Event* event, Host* srcHost, Host* dstHost, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    ThreadSinglePolicyData* data = policy->data;

    /* non-local events must be properly delayed so the event wont show up at another worker
     * before the next scheduling interval. this is only a problem if the sender and
     * receivers have been assigned to different worker threads. */
    GThread* srcThread = g_hash_table_lookup(data->hostToThreadMap, srcHost);
    GThread* dstThread = g_hash_table_lookup(data->hostToThreadMap, dstHost);

    SimulationTime eventTime = event_getTime(event);

    if(srcThread != dstThread && eventTime < barrier) {
        event_setTime(event, barrier);
        info("Inter-host event time %"G_GUINT64_FORMAT" changed to %"G_GUINT64_FORMAT" "
                "to ensure event causality", eventTime, barrier);
    }

    /* get the queue for the destination */
    ThreadSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, dstThread);
    utility_assert(tdata);

    /* 'deliver' the event there */
    g_mutex_lock(&(tdata->lock));
    event_setSequence(event, ++(tdata->pushSequenceCounter));
    priorityqueue_push(tdata->pq, event);
    tdata->nPushed++;
    g_mutex_unlock(&(tdata->lock));
}

static Event* _schedulerpolicythreadsingle_pop(SchedulerPolicy* policy, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    ThreadSinglePolicyData* data = policy->data;

    /* figure out which hosts we should be checking */
    ThreadSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, g_thread_self());
    /* if there is no tdata, that means this thread didn't get any hosts assigned to it */
    if(!tdata) {
        /* this thread will remain idle */
        return NULL;
    }

    g_mutex_lock(&(tdata->lock));

    Event* nextEvent = priorityqueue_peek(tdata->pq);
    SimulationTime eventTime = (nextEvent != NULL) ? event_getTime(nextEvent) : SIMTIME_INVALID;

    if(nextEvent && eventTime < barrier) {
        utility_assert(eventTime >= tdata->lastEventTime);
        tdata->lastEventTime = eventTime;
        nextEvent = priorityqueue_pop(tdata->pq);
        tdata->nPopped++;
    } else {
        /* if we make it here, all hosts for this thread have no more events before barrier */
        nextEvent = NULL;
    }

    g_mutex_unlock(&(tdata->lock));

    return nextEvent;
}

static SimulationTime _schedulerpolicythreadsingle_getNextTime(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    ThreadSinglePolicyData* data = policy->data;

    SimulationTime nextTime = SIMTIME_MAX;

    ThreadSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, g_thread_self());
    if(tdata) {
        g_mutex_lock(&(tdata->lock));
        Event* event = priorityqueue_peek(tdata->pq);
        g_mutex_unlock(&(tdata->lock));
        if(event != NULL) {
            nextTime = MIN(nextTime, event_getTime(event));
        }
    }

    return nextTime;
}

static void _schedulerpolicythreadsingle_free(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    ThreadSinglePolicyData* data = policy->data;

    g_hash_table_destroy(data->threadToThreadDataMap);
    g_hash_table_destroy(data->hostToThreadMap);
    g_free(data);

    MAGIC_CLEAR(policy);
    g_free(policy);
}

SchedulerPolicy* schedulerpolicythreadsingle_new() {
    ThreadSinglePolicyData* data = g_new0(ThreadSinglePolicyData, 1);
    data->threadToThreadDataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_threadsinglethreaddata_free);
    data->hostToThreadMap = g_hash_table_new(g_direct_hash, g_direct_equal);

    SchedulerPolicy* policy = g_new0(SchedulerPolicy, 1);
    MAGIC_INIT(policy);
    policy->addHost = _schedulerpolicythreadsingle_addHost;
    policy->getAssignedHosts = _schedulerpolicythreadsingle_getHosts;
    policy->push = _schedulerpolicythreadsingle_push;
    policy->pop = _schedulerpolicythreadsingle_pop;
    policy->getNextTime = _schedulerpolicythreadsingle_getNextTime;
    policy->free = _schedulerpolicythreadsingle_free;

    policy->type = SP_PARALLEL_THREAD_SINGLE;
    policy->data = data;
    policy->referenceCount = 1;

    return policy;
}

