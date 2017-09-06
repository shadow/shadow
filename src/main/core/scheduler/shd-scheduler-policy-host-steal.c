/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef struct _HostStealQueueData HostStealQueueData;
struct _HostStealQueueData {
    GMutex lock;
    PriorityQueue* pq;
    SimulationTime pushSequenceCounter;
    SimulationTime lastEventTime;
    gsize nPushed;
    gsize nPopped;
};

typedef struct _HostStealThreadData HostStealThreadData;
struct _HostStealThreadData {
    /* used to cache getHosts() result for memory management as needed*/
    GQueue* allHosts;
    /* All hosts that have been assigned to this worker for event processing that have not
     * been started this round. Other than the first round, this is last round's processedHosts. */
    GQueue* unprocessedHosts;
    /* during each round, hosts whose events have been processed are moved from some thread's
     * unprocessedHosts to here, via runningHost */
    GQueue* processedHosts;
    /* the host this worker is running; belongs to neither unprocessedHosts nor processedHosts */
    Host* runningHost;
    SimulationTime currentBarrier;
    GTimer* pushIdleTime;
    GTimer* popIdleTime;
    /* which worker thread this is */
    guint tnumber;
    GMutex lock;
};

typedef struct _HostStealPolicyData HostStealPolicyData;
struct _HostStealPolicyData {
    GArray* threadList;
    guint threadCount;
    GHashTable* hostToQueueDataMap;
    GHashTable* threadToThreadDataMap;
    GHashTable* hostToThreadMap;
    GRWLock lock;
    MAGIC_DECLARE;
};

typedef struct _HostStealSearchState HostStealSearchState;
struct _HostStealSearchState {
    HostStealPolicyData* data;
    SimulationTime nextEventTime;
};

static HostStealThreadData* _hoststealthreaddata_new() {
    HostStealThreadData* tdata = g_new0(HostStealThreadData, 1);

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
    g_mutex_init(&(tdata->lock));
    tdata->runningHost = NULL;
    return tdata;
}

static void _hoststealthreaddata_free(HostStealThreadData* tdata) {
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

static HostStealQueueData* _hoststealqueuedata_new() {
    HostStealQueueData* qdata = g_new0(HostStealQueueData, 1);

    g_mutex_init(&(qdata->lock));
    qdata->pq = priorityqueue_new((GCompareDataFunc)event_compare, NULL, (GDestroyNotify)event_unref);

    return qdata;
}

static void _hoststealqueuedata_free(HostStealQueueData* qdata) {
    if(qdata) {
        if(qdata->pq) {
            priorityqueue_free(qdata->pq);
        }
        g_mutex_clear(&(qdata->lock));
        g_free(qdata);
    }
}

/* this must be run synchronously, or the thread must be protected by locks */
static void _schedulerpolicyhoststeal_addHost(SchedulerPolicy* policy, Host* host, pthread_t randomThread) {
    MAGIC_ASSERT(policy);
    HostStealPolicyData* data = policy->data;

    /* each host has its own queue
     * we don't read lock data->lock because we only modify the table here anyway
     */
    if(!g_hash_table_lookup(data->hostToQueueDataMap, host)) {
        g_rw_lock_writer_lock(&data->lock);
        g_hash_table_replace(data->hostToQueueDataMap, host, _hoststealqueuedata_new());
        g_rw_lock_writer_unlock(&data->lock);
    }

    /* each thread keeps track of the hosts it needs to run */
    pthread_t assignedThread = (randomThread != 0) ? randomThread : pthread_self();
    g_rw_lock_reader_lock(&data->lock);
    HostStealThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread));
    g_rw_lock_reader_unlock(&data->lock);
    if(!tdata) {
        tdata = _hoststealthreaddata_new();
        g_rw_lock_writer_lock(&data->lock);
        g_hash_table_replace(data->threadToThreadDataMap, GUINT_TO_POINTER(assignedThread), tdata);
        tdata->tnumber = data->threadCount;
        data->threadCount++;
        g_array_append_val(data->threadList, tdata);
    } else {
        g_rw_lock_writer_lock(&data->lock);
    }
    /* store the host-to-thread mapping */
    g_hash_table_replace(data->hostToThreadMap, host, GUINT_TO_POINTER(assignedThread));
    g_rw_lock_writer_unlock(&data->lock);
    /* if the target thread is stealing the host, we don't want to add it twice */
    if(host != tdata->runningHost) {
        g_queue_push_tail(tdata->unprocessedHosts, host);
    }
}

/* primarily a wrapper for dealing with TLS and the hostToThread map.
 * this does not affect unprocessedHosts/processedHosts/runningHost;
 * that migration should be done as normal (from/to the respective threads) */
static void _schedulerpolicyhoststeal_migrateHost(SchedulerPolicy* policy, Host* host, pthread_t newThread) {
    MAGIC_ASSERT(policy);
    HostStealPolicyData* data = policy->data;
    g_rw_lock_reader_lock(&data->lock);
    pthread_t oldThread = (pthread_t)g_hash_table_lookup(data->hostToThreadMap, host);
    if(oldThread == newThread) {
        g_rw_lock_reader_unlock(&data->lock);
        return;
    }
    HostStealThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(oldThread));
    HostStealThreadData* tdataNew = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(newThread));
    g_rw_lock_reader_unlock(&data->lock);
    /* check that there's actually a thread we're migrating from */
    if(tdata) {
        /* Sanity check that the host isn't being run on another thread while migrating.
         * Ostensibly, we could make this check on *all* threads, but this is simpler, faster,
         * and should catch most bugs (since it's presumably the thread we're stealing from
         * that would be running it).
         */
        utility_assert(tdata->runningHost != tdataNew->runningHost);
        /* migrate the TLS of all objects associated with this host */
        host_migrate(host, &oldThread, &newThread);
    }
    _schedulerpolicyhoststeal_addHost(policy, host, newThread);
}

static void concat_queue_iter(Host* hostItem, GQueue* userQueue) {
    g_queue_push_tail(userQueue, hostItem);
}

static GQueue* _schedulerpolicyhoststeal_getHosts(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostStealPolicyData* data = policy->data;
    g_rw_lock_reader_lock(&data->lock);
    HostStealThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    g_rw_lock_reader_unlock(&data->lock);
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

static void _schedulerpolicyhoststeal_push(SchedulerPolicy* policy, Event* event, Host* srcHost, Host* dstHost, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    HostStealPolicyData* data = policy->data;

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

    g_rw_lock_reader_lock(&data->lock);
    /* we want to track how long this thread spends idle waiting to push the event */
    HostStealThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));

    /* get the queue for the destination */
    HostStealQueueData* qdata = g_hash_table_lookup(data->hostToQueueDataMap, dstHost);
    g_rw_lock_reader_unlock(&data->lock);
    utility_assert(qdata);

    /* tracking idle time spent waiting for the destination queue lock */
    if(tdata) {
        g_timer_continue(tdata->pushIdleTime);
        g_mutex_lock(&(tdata->lock));
    }
    g_mutex_lock(&(qdata->lock));
    if(tdata) {
        g_timer_stop(tdata->pushIdleTime);
    }

    /* 'deliver' the event to the destination queue */
    event_setSequence(event, ++(qdata->pushSequenceCounter));
    priorityqueue_push(qdata->pq, event);
    qdata->nPushed++;

    /* release the destination queue lock */
    g_mutex_unlock(&(qdata->lock));
    if(tdata) {
        g_mutex_unlock(&(tdata->lock));
    }
}

static Event* _schedulerpolicyhoststeal_popFromThread(SchedulerPolicy* policy, HostStealThreadData* tdata, GQueue* assignedHosts, SimulationTime barrier) {
    /* if there is no tdata, that means this thread didn't get any hosts assigned to it */
    if(!tdata) {
        return NULL;
    }

    HostStealPolicyData* data = policy->data;

    while(!g_queue_is_empty(assignedHosts) || tdata->runningHost) {
        /* if there's no running host, we completed the last assignment and need a new one */
        if(!tdata->runningHost) {
            tdata->runningHost = g_queue_pop_head(assignedHosts);
        }
        Host* host = tdata->runningHost;
        g_rw_lock_reader_lock(&data->lock);
        HostStealQueueData* qdata = g_hash_table_lookup(data->hostToQueueDataMap, host);
        g_rw_lock_reader_unlock(&data->lock);
        utility_assert(qdata);

        g_mutex_lock(&(qdata->lock));
        Event* nextEvent = priorityqueue_peek(qdata->pq);
        SimulationTime eventTime = (nextEvent != NULL) ? event_getTime(nextEvent) : SIMTIME_INVALID;

        if(nextEvent != NULL && eventTime < barrier) {
            utility_assert(eventTime >= qdata->lastEventTime);
            qdata->lastEventTime = eventTime;
            nextEvent = priorityqueue_pop(qdata->pq);
            qdata->nPopped++;
            /* migrate iff a migration is needed */
            _schedulerpolicyhoststeal_migrateHost(policy, host, pthread_self());
        } else {
            nextEvent = NULL;
        }

        if(nextEvent == NULL) {
            /* no more events on the runningHost, mark it as NULL so we get a new one */
            g_queue_push_tail(tdata->processedHosts, host);
            tdata->runningHost = NULL;
        }

        g_mutex_unlock(&(qdata->lock));

        if(nextEvent != NULL) {
            return nextEvent;
        }
    }

    /* if we make it here, all hosts for this thread have no more events before barrier */
    return NULL;
}

static Event* _schedulerpolicyhoststeal_pop(SchedulerPolicy* policy, SimulationTime barrier) {
    MAGIC_ASSERT(policy);
    HostStealPolicyData* data = policy->data;

    /* first, we try to pop a host from this thread's queue */
    g_rw_lock_reader_lock(&data->lock);
    HostStealThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    g_rw_lock_reader_unlock(&data->lock);

    /* we only need to lock this thread's lock, since it's our own queue */
    g_timer_continue(tdata->popIdleTime);
    g_mutex_lock(&(tdata->lock));
    g_timer_stop(tdata->popIdleTime);

    if(tdata != NULL && barrier > tdata->currentBarrier) {
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
    /* attempt to get an event from this thread's queue */
    Event* nextEvent = _schedulerpolicyhoststeal_popFromThread(policy, tdata, tdata->unprocessedHosts, barrier);
    g_mutex_unlock(&(tdata->lock));
    if(nextEvent != NULL) {
        return nextEvent;
    }

    /* no more hosts with events on this thread, try to steal a host from the other threads' queues */
    GHashTableIter iter;
    gpointer key, value;
    g_rw_lock_reader_lock(&data->lock);
    guint i, n = data->threadCount;
    g_rw_lock_reader_unlock(&data->lock);
    for(i = 1; i < n; i++) {
        guint stolenTnumber = (i + tdata->tnumber) % n;
        g_rw_lock_reader_lock(&data->lock);
        HostStealThreadData* stolenTdata = g_array_index(data->threadList, HostStealThreadData*, stolenTnumber);
        g_rw_lock_reader_unlock(&data->lock);
        /* We don't need a lock here, because we're only reading, and a misread just means either
         * we read as empty when it's not, in which case the assigned thread (or one of the others)
         * will pick it up anyway, or it reads as non-empty when it is empty, in which case we'll
         * just get a NULL event and move on. Accepting this reduces lock contention towards the end
         * of every round. */
        if(g_queue_is_empty(stolenTdata->unprocessedHosts)) {
            continue;
        }
        /* We need to lock the thread we're stealing from, to be sure that we're not stealing
         * something already being stolen, as well as our own lock, to be sure nobody steals
         * what we just stole. But we also need to do this in a well-ordered manner, to
         * prevent deadlocks. To do this, we always lock the lock with the smaller thread
         * number first. */
        g_timer_continue(tdata->popIdleTime);
        if(tdata->tnumber < stolenTnumber) {
            g_mutex_lock(&(tdata->lock));
            g_mutex_lock(&(stolenTdata->lock));
        } else {
            g_mutex_lock(&(stolenTdata->lock));
            g_mutex_lock(&(tdata->lock));
        }
        g_timer_stop(tdata->popIdleTime);

        /* attempt to get event from the other thread's queue, likely moving a host from its
         * unprocessedHosts into this threads runningHost (and eventually processedHosts) */
        nextEvent = _schedulerpolicyhoststeal_popFromThread(policy, tdata, stolenTdata->unprocessedHosts, barrier);

        /* must unlock in reverse order of locking */
        if(tdata->tnumber < stolenTnumber) {
            g_mutex_unlock(&(stolenTdata->lock));
            g_mutex_unlock(&(tdata->lock));
        } else {
            g_mutex_unlock(&(tdata->lock));
            g_mutex_unlock(&(stolenTdata->lock));
        }

        if(nextEvent != NULL) {
            break;
        }
    }
    return nextEvent;
}

static void _schedulerpolicyhoststeal_findMinTime(Host* host, HostStealSearchState* state) {
    g_rw_lock_reader_lock(&state->data->lock);
    HostStealQueueData* qdata = g_hash_table_lookup(state->data->hostToQueueDataMap, host);
    g_rw_lock_reader_unlock(&state->data->lock);
    utility_assert(qdata);

    g_mutex_lock(&(qdata->lock));
    Event* event = priorityqueue_peek(qdata->pq);
    g_mutex_unlock(&(qdata->lock));

    if(event != NULL) {
        state->nextEventTime = MIN(state->nextEventTime, event_getTime(event));
    }
}

static SimulationTime _schedulerpolicyhoststeal_getNextTime(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostStealPolicyData* data = policy->data;

    /* set up state that we need for the foreach queue iterator */
    HostStealSearchState searchState;
    memset(&searchState, 0, sizeof(HostStealSearchState));
    searchState.data = data;
    searchState.nextEventTime = SIMTIME_MAX;

    g_rw_lock_reader_lock(&data->lock);
    HostStealThreadData* tdata = g_hash_table_lookup(data->threadToThreadDataMap, GUINT_TO_POINTER(pthread_self()));
    g_rw_lock_reader_unlock(&data->lock);
    if(tdata) {
        /* make sure we get all hosts, which are probably held in the processedHosts queue between rounds */
        g_queue_foreach(tdata->unprocessedHosts, (GFunc)_schedulerpolicyhoststeal_findMinTime, &searchState);
        g_queue_foreach(tdata->processedHosts, (GFunc)_schedulerpolicyhoststeal_findMinTime, &searchState);
    }
    info("next event at time %"G_GUINT64_FORMAT, searchState.nextEventTime);

    return searchState.nextEventTime;
}

static void _schedulerpolicyhoststeal_free(SchedulerPolicy* policy) {
    MAGIC_ASSERT(policy);
    HostStealPolicyData* data = policy->data;

    g_hash_table_destroy(data->hostToQueueDataMap);
    g_hash_table_destroy(data->threadToThreadDataMap);
    g_hash_table_destroy(data->hostToThreadMap);
    g_rw_lock_clear(&data->lock);
    g_free(data);

    MAGIC_CLEAR(policy);
    g_free(policy);
}

SchedulerPolicy* schedulerpolicyhoststeal_new() {
    HostStealPolicyData* data = g_new0(HostStealPolicyData, 1);
    data->threadList = g_array_new(FALSE, FALSE, sizeof(HostStealThreadData*));
    data->hostToQueueDataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_hoststealqueuedata_free);
    data->threadToThreadDataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)_hoststealthreaddata_free);
    data->hostToThreadMap = g_hash_table_new(g_direct_hash, g_direct_equal);
    g_rw_lock_init(&data->lock);

    SchedulerPolicy* policy = g_new0(SchedulerPolicy, 1);
    MAGIC_INIT(policy);
    policy->addHost = _schedulerpolicyhoststeal_addHost;
    policy->getAssignedHosts = _schedulerpolicyhoststeal_getHosts;
    policy->push = _schedulerpolicyhoststeal_push;
    policy->pop = _schedulerpolicyhoststeal_pop;
    policy->getNextTime = _schedulerpolicyhoststeal_getNextTime;
    policy->free = _schedulerpolicyhoststeal_free;

    policy->type = SP_PARALLEL_HOST_STEAL;
    policy->data = data;
    policy->referenceCount = 1;

    return policy;
}
