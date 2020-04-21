/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <glib.h>
#include <pthread.h>
#include <string.h>

#include "core/logger/logger.h"
#include "core/scheduler/scheduler_policy.h"
#include "core/support/definitions.h"
#include "core/work/event.h"
#include "host/host.h"
#include "utility/priority_queue.h"
#include "utility/utility.h"

typedef struct _HostSingleQueueData HostSingleQueueData;
struct _HostSingleQueueData {
    GMutex lock;
    PriorityQueue* pq;
    SimulationTime lastEventTime;
    gsize nPushed;
    gsize nPopped;
};

typedef struct _HostSingleThreadData HostSingleThreadData;
struct _HostSingleThreadData {
    /* used to cache getHosts() result for memory management as needed */
    GQueue* allHosts;
    /* all hosts that have been assigned to this worker for event processing but not yet processed this round */
    GQueue* unprocessedHosts;
    /* during each round, hosts whose events have been processed are moved from unprocessedHosts to here */
    GQueue* processedHosts;
    SimulationTime currentBarrier;
    GTimer* pushIdleTime;
    GTimer* popIdleTime;
};

typedef struct _HostSinglePolicyData HostSinglePolicyData;
struct _HostSinglePolicyData {
    GHashTable* hostToQueueDataMap;
    GHashTable* threadToThreadDataMap;
    GHashTable* hostToThreadMap;
    MAGIC_DECLARE;
};

typedef struct _HostSingleSearchState HostSingleSearchState;
struct _HostSingleSearchState {
    HostSinglePolicyData* data;
    SimulationTime nextEventTime;
};

static HostSingleThreadData* _hostsinglethreaddata_new() {
    HostSingleThreadData* tdata = g_new0(HostSingleThreadData, 1);

    tdata->unprocessedHosts = g_queue_new();
    tdata->processedHosts = g_queue_new();

    /* Create new timers to track thread idle times. The timers start in a 'started' state,
     * so we want to stop them immediately so we can continue/stop later around blocking code
     * to collect total elapsed idle time in the scheduling process throughout the entire
     * runtime of the program. */
    tdata->pushIdleTime = g_timer_new();
    g_timer_stop(tdata->pushIdleTime);
    tdata->popIdleTime = g_timer_new();
    g_timer_stop(tdata->popIdleTime);
    return tdata;
}

static void _hostsinglethreaddata_free(HostSingleThreadData* tdata) {
    if(tdata) {
        if(tdata->allHosts) {
            g_queue_free(tdata->allHosts);
        }
        if(tdata->unprocessedHosts) {
            g_queue_free(tdata->unprocessedHosts);
        }
        if(tdata->processedHosts) {
            g_queue_free(tdata->processedHosts);
        }

        gdouble totalPushWaitTime = 0.0;
        if(tdata->pushIdleTime) {
            totalPushWaitTime = g_timer_elapsed(tdata->pushIdleTime, NULL);
            g_timer_destroy(tdata->pushIdleTime);
        }
        gdouble totalPopWaitTime = 0.0;
        if(tdata->popIdleTime) {
            totalPopWaitTime = g_timer_elapsed(tdata->popIdleTime, NULL);
            g_timer_destroy(tdata->popIdleTime);
        }

        g_free(tdata);
        message("scheduler thread data destroyed, total push wait time was %f seconds, "
                "total pop wait time was %f seconds", totalPushWaitTime, totalPopWaitTime);
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
static void _schedulerpolicyhostsingle_addHost(SchedulerPolicy* policy, Host* host, pthread_t randomThread) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    /* each host has its own queue */
    if(!g_hash_table_lookup(data->hostToQueueDataMap, host)) {
        g_hash_table_replace(data->hostToQueueDataMap, host, _hostsinglequeuedata_new());
    }

    /* each thread keeps track of the hosts it needs to run */
    pthread_t assignedThread = (randomThread != 0) ? randomThread : pthread_self();
    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread));
    if(!tdata) {
        tdata = _hostsinglethreaddata_new();
        g_hash_table_replace(data->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread), tdata);
    }
    g_queue_push_tail(tdata->unprocessedHosts, host);

    /* finally, store the host-to-thread mapping */
    g_hash_table_replace(data->hostToThreadMap, host, GUINT_TO_POINTER(assignedThread));
}

static void concat_queue_iter(Host* hostItem, GQueue* userQueue) {
    g_queue_push_tail(userQueue, hostItem);
}

static GQueue* _schedulerpolicyhostsingle_getHosts(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;
    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    if(!tdata) {
        return NULL;
    }
    if(g_queue_is_empty(tdata->unprocessedHosts)) {
        return tdata->processedHosts;
    }
    if(g_queue_is_empty(tdata->processedHosts)) {
        return tdata->unprocessedHosts;
    }
    if(tdata->allHosts) {
        g_queue_free(tdata->allHosts);
    }
    tdata->allHosts = g_queue_copy(tdata->processedHosts);
    g_queue_foreach(tdata->unprocessedHosts, (GFunc)concat_queue_iter, tdata->allHosts);
    return tdata->allHosts;
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

    /* we want to track how long this thread spends idle waiting to push the event */
    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));

    /* get the queue for the destination */
    HostSingleQueueData* qdata = g_hash_table_lookup(data->hostToQueueDataMap, dstHost);
    utility_assert(qdata);

    /* tracking idle time spent waiting for the destination queue lock */
    if(tdata) {
        g_timer_continue(tdata->pushIdleTime);
    }
    g_mutex_lock(&(qdata->lock));
    if(tdata) {
        g_timer_stop(tdata->pushIdleTime);
    }

    /* 'deliver' the event to the destination queue */
    priorityqueue_push(qdata->pq, event);
    qdata->nPushed++;

    /* release the destination queue lock */
    g_mutex_unlock(&(qdata->lock));
}

static Event* _schedulerpolicyhostsingle_pop(SchedulerPolicy* policy, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    /* figure out which hosts we should be checking */
    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    /* if there is no tdata, that means this thread didn't get any hosts assigned to it */
    if(!tdata) {
        /* this thread will remain idle */
        return NULL;
    }

    if(barrier > tdata->currentBarrier) {
        tdata->currentBarrier = barrier;

        /* make sure all of the hosts that were processed last time get processed in the next round */
        if(g_queue_is_empty(tdata->unprocessedHosts) && !g_queue_is_empty(tdata->processedHosts)) {
            GQueue* swap = tdata->unprocessedHosts;
            tdata->unprocessedHosts = tdata->processedHosts;
            tdata->processedHosts = swap;
        } else {
            while(!g_queue_is_empty(tdata->processedHosts)) {
                g_queue_push_tail(tdata->unprocessedHosts, g_queue_pop_head(tdata->processedHosts));
            }
        }
    }

    while(!g_queue_is_empty(tdata->unprocessedHosts)) {
        Host* host = g_queue_peek_head(tdata->unprocessedHosts);
        HostSingleQueueData* qdata = g_hash_table_lookup(data->hostToQueueDataMap, host);
        utility_assert(qdata);

        /* tracking idle time spent waiting for the host queue lock */
        g_timer_continue(tdata->popIdleTime);
        g_mutex_lock(&(qdata->lock));
        g_timer_stop(tdata->popIdleTime);

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
        /* this host is done, store it in the processed queue and then
         * try the next host if we still have more */
        g_queue_push_tail(tdata->processedHosts, g_queue_pop_head(tdata->unprocessedHosts));
    }

    /* if we make it here, all hosts for this thread have no more events before barrier */
    return NULL;
}

static void _schedulerpolicyhostsingle_findMinTime(Host* host, HostSingleSearchState* state) {
    HostSingleQueueData* qdata = g_hash_table_lookup(state->data->hostToQueueDataMap, host);
    utility_assert(qdata);

    g_mutex_lock(&(qdata->lock));
    Event* event = priorityqueue_peek(qdata->pq);
    g_mutex_unlock(&(qdata->lock));

    if(event != NULL) {
        state->nextEventTime = MIN(state->nextEventTime, event_getTime(event));
    }
}

static SimulationTime _schedulerpolicyhostsingle_getNextTime(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostSinglePolicyData* data = policy->data;

    /* set up state that we need for the foreach queue iterator */
    HostSingleSearchState searchState;
    memset(&searchState, 0, sizeof(HostSingleSearchState));
    searchState.data = data;
    searchState.nextEventTime = SIMTIME_MAX;

    HostSingleThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    if(tdata) {
        /* make sure we get all hosts, which are probably held in the processedHosts queue between rounds */
        g_queue_foreach(tdata->unprocessedHosts, (GFunc)_schedulerpolicyhostsingle_findMinTime, &searchState);
        g_queue_foreach(tdata->processedHosts, (GFunc)_schedulerpolicyhostsingle_findMinTime, &searchState);
    }
    info("next event at time %"G_GUINT64_FORMAT, searchState.nextEventTime);

    return searchState.nextEventTime;
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

