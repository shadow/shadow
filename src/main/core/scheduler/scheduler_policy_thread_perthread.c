/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <pthread.h>
#include <stddef.h>

#include "main/core/scheduler/scheduler_policy.h"
#include "main/core/support/definitions.h"
#include "main/core/work/event.h"
#include "main/host/host.h"
#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

typedef struct _ThreadPerThreadQueueData ThreadPerThreadQueueData;
struct _ThreadPerThreadQueueData {
    PriorityQueue* pq;
    SimulationTime lastEventTime;
    gsize nPushed;
    gsize nPopped;
};

typedef struct _ThreadPerThreadThreadData ThreadPerThreadThreadData;
struct _ThreadPerThreadThreadData {
    GMutex lock;
    GQueue* assignedHosts;
    /* the main event queue for this thread */
    ThreadPerThreadQueueData* qdata;
    /* this thread has gqueue that holds future events during each round, and is emptied into
     * the priority queue in qdata after each round */
    GHashTable* threadToPQueueMap;
};

typedef struct _ThreadPerThreadPolicyData ThreadPerThreadPolicyData;
struct _ThreadPerThreadPolicyData {
    GHashTable* threadToThreadDataMap;
    GHashTable* hostToThreadMap;
    MAGIC_DECLARE;
};

static ThreadPerThreadQueueData* _threadperthreadqueuedata_new() {
    ThreadPerThreadQueueData* qdata = g_new0(ThreadPerThreadQueueData, 1);

    qdata->pq = priorityqueue_new((GCompareDataFunc)event_compare, NULL, (GDestroyNotify)event_unref);

    return qdata;
}

static void _threadperthreadqueuedata_free(ThreadPerThreadQueueData* qdata) {
    if(qdata) {
        if(qdata->pq) {
            priorityqueue_free(qdata->pq);
        }
        g_free(qdata);
    }
}

static ThreadPerThreadThreadData* _threadperthreadthreaddata_new() {
    ThreadPerThreadThreadData* tdata = g_new0(ThreadPerThreadThreadData, 1);
    tdata->threadToPQueueMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)priorityqueue_free);
    tdata->qdata = _threadperthreadqueuedata_new();
    tdata->assignedHosts = g_queue_new();
    g_mutex_init(&(tdata->lock));
    return tdata;
}

static void _threadperthreadthreaddata_free(ThreadPerThreadThreadData* tdata) {
    if(tdata) {
        if(tdata->assignedHosts) {
            g_queue_free(tdata->assignedHosts);
        }
        if(tdata->threadToPQueueMap) {
            g_hash_table_destroy(tdata->threadToPQueueMap);
        }
        _threadperthreadqueuedata_free(tdata->qdata);
        g_mutex_clear(&(tdata->lock));
        g_free(tdata);
    }
}

/* this must be run synchronously, or the call must be protected by locks */
static void _schedulerpolicythreadperthread_addHost(SchedulerPolicy* policy, Host* host, pthread_t randomThread) {
    MAGIC_ASSERT(policy);
    ThreadPerThreadPolicyData* data = policy->data;

    /* each thread keeps track of the hosts it needs to run */
    pthread_t assignedThread = (randomThread != 0) ? randomThread : pthread_self();
    ThreadPerThreadThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread));
    if(!tdata) {
        tdata = _threadperthreadthreaddata_new();
        g_hash_table_replace(data->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread), tdata);
    }
    g_queue_push_tail(tdata->assignedHosts, host);

    /* finally, store the host-to-thread mapping */
    g_hash_table_replace(data->hostToThreadMap, host, GUINT_TO_POINTER(assignedThread));
}

static GQueue* _schedulerpolicythreadperthread_getHosts(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    ThreadPerThreadPolicyData* data = policy->data;
    ThreadPerThreadThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    return (tdata != NULL) ? tdata->assignedHosts : NULL;
}

static void _schedulerpolicythreadperthread_push(SchedulerPolicy* policy, Event* event, Host* srcHost, Host* dstHost, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    ThreadPerThreadPolicyData* data = policy->data;

    /* non-local events must be properly delayed so the event wont show up at another worker
     * before the next scheduling interval. this is only a problem if the sender and
     * receivers have been assigned to different worker threads. */
    pthread_t srcThread = GPOINTER_TO_UINT(g_hash_table_lookup(data->hostToThreadMap, srcHost));
    pthread_t dstThread = GPOINTER_TO_UINT(g_hash_table_lookup(data->hostToThreadMap, dstHost));

    SimulationTime eventTime = event_getTime(event);

    if(!pthread_equal(srcThread, dstThread) && eventTime < barrier) {
        event_setTime(event, barrier);
        info("Inter-host event time %"G_GUINT64_FORMAT" changed to %"G_GUINT64_FORMAT" "
                "to ensure event causality", eventTime, barrier);
    }

    /* get the queue for the destination */
    ThreadPerThreadThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(dstThread));
    utility_assert(tdata);

    pthread_t self = pthread_self();
    if(pthread_equal(dstThread, self)) {
        priorityqueue_push(tdata->qdata->pq, event);
        tdata->qdata->nPushed++;
    } else {
        /* we need to lock this if srcThread != pthread_self */
        if(!pthread_equal(srcThread, self)) {
            g_mutex_lock(&(tdata->lock));
        }

        /* now make sure we have a mailbox for the source and create one if needed */
        PriorityQueue* futureEvents = g_hash_table_lookup(tdata->threadToPQueueMap, GUINT_TO_POINTER(srcThread));
        if(!futureEvents) {
            futureEvents = priorityqueue_new((GCompareDataFunc)event_compare, NULL, (GDestroyNotify)event_unref);
            g_hash_table_replace(tdata->threadToPQueueMap, GUINT_TO_POINTER(srcThread), futureEvents);
        }

        /* 'deliver' the event there */
        priorityqueue_push(futureEvents, event);

        if(!pthread_equal(srcThread, self)) {
            g_mutex_unlock(&(tdata->lock));
        }
    }
}

static Event* _schedulerpolicythreadperthread_pop(SchedulerPolicy* policy, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    ThreadPerThreadPolicyData* data = policy->data;

    /* figure out which hosts we should be checking */
    ThreadPerThreadThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    /* if there is no tdata, that means this thread didn't get any hosts assigned to it */
    if(!tdata) {
        /* this thread will remain idle */
        return NULL;
    }

    Event* nextEvent = priorityqueue_peek(tdata->qdata->pq);
    SimulationTime eventTime = (nextEvent != NULL) ? event_getTime(nextEvent) : SIMTIME_INVALID;

    if(nextEvent && eventTime < barrier) {
        utility_assert(eventTime >= tdata->qdata->lastEventTime);
        tdata->qdata->lastEventTime = eventTime;
        nextEvent = priorityqueue_pop(tdata->qdata->pq);
        tdata->qdata->nPopped++;
    } else {
        /* if we make it here, all hosts for this thread have no more events before barrier */
        nextEvent = NULL;
    }

    return nextEvent;
}

static SimulationTime _schedulerpolicythreadperthread_getNextTime(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    ThreadPerThreadPolicyData* data = policy->data;

    SimulationTime nextTime = SIMTIME_MAX;

    ThreadPerThreadThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    if(tdata) {
        /* we are in between rounds. first we have to drain all future events into the priority queue */
        GList* values = g_hash_table_get_values(tdata->threadToPQueueMap);
        GList* item = values;
        while(item) {
            PriorityQueue* futureEvents = item->data;

            while(!priorityqueue_isEmpty(futureEvents)) {
                Event* event = priorityqueue_pop(futureEvents);
                priorityqueue_push(tdata->qdata->pq, event);
                tdata->qdata->nPushed++;
            }

            item = g_list_next(item);
        }
        if(values) {
            g_list_free(values);
        }

        /* now get the min time */
        Event* nextEvent = priorityqueue_peek(tdata->qdata->pq);
        if(nextEvent != NULL) {
            nextTime = MIN(nextTime, event_getTime(nextEvent));
        }
    }

    return nextTime;
}

static void _schedulerpolicythreadperthread_free(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    ThreadPerThreadPolicyData* data = policy->data;

    if(data) {
        if(data->threadToThreadDataMap) {
            g_hash_table_destroy(data->threadToThreadDataMap);
        }
        if(data->hostToThreadMap) {
            g_hash_table_destroy(data->hostToThreadMap);
        }
        g_free(data);
    }

    MAGIC_CLEAR(policy);
    g_free(policy);
}

SchedulerPolicy* schedulerpolicythreadperthread_new() {
    ThreadPerThreadPolicyData* data = g_new0(ThreadPerThreadPolicyData, 1);
    data->threadToThreadDataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_threadperthreadthreaddata_free);
    data->hostToThreadMap = g_hash_table_new(g_direct_hash, g_direct_equal);

    SchedulerPolicy* policy = g_new0(SchedulerPolicy, 1);
    MAGIC_INIT(policy);
    policy->addHost = _schedulerpolicythreadperthread_addHost;
    policy->getAssignedHosts = _schedulerpolicythreadperthread_getHosts;
    policy->push = _schedulerpolicythreadperthread_push;
    policy->pop = _schedulerpolicythreadperthread_pop;
    policy->getNextTime = _schedulerpolicythreadperthread_getNextTime;
    policy->free = _schedulerpolicythreadperthread_free;

    policy->type = SP_PARALLEL_THREAD_PERTHREAD;
    policy->data = data;
    policy->referenceCount = 1;

    return policy;
}

