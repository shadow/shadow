/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* manages the scheduling of events and hosts to threads,
 * following one of several scheduling policies */
struct _Scheduler {
    /* all worker threads used by the scheduler */
    GList* workerThreads;

    /* global lock for all threads, hold this as little as possible */
    GMutex globalLock;

    /* barrier for worker threads to start and stop running */
    CountDownLatch* startBarrier;
    CountDownLatch* finishBarrier;
    /* barrier to wait for worker threads to finish processing this round */
    CountDownLatch* executeEventsBarrier;
    /* barrier to wait for worker threads to collect info after a round */
    CountDownLatch* collectInfoBarrier;
    /* barrier to wait for main thread to finish updating for the next round */
    CountDownLatch* prepareRoundBarrier;

    /* the serial/parallel host/thread mapping/scheduling policy */
    SchedulerPolicy* policy;
    SchedulerPolicyType policyType;

    /* we store the hosts here */
    GHashTable* hostIDToHostMap;

    /* used to randomize host-to-thread assignment */
    Random* random;

    /* auxiliary information about current running state */
    gboolean isRunning;
    SimulationTime endTime;
    struct {
        SimulationTime endTime;
        SimulationTime minNextEventTime;
    } currentRound;

    /* for memory management */
    gint referenceCount;
    MAGIC_DECLARE;
};

static void _scheduler_rebalanceHosts(Scheduler* scheduler) {
    MAGIC_ASSERT(scheduler);

    // WARNING if this is run, then all existing eventSequenceCounters
    // need to get set to the max of all existing counters to ensure order correctness
    // also, we probably would need to update the ProgramState for each virtual process
    // if we move a host to a different thread, because the other thread has its state
    // at different memory regions because it opened separate libraries

    // we should add timers to each thread so we know which ones are overloaded or not

    GList* allHosts = g_hash_table_get_values(scheduler->hostIDToHostMap);
    GList* hostIter = g_list_first(allHosts);
    GList* threadIter = g_list_first(scheduler->workerThreads);

    while(hostIter) {
        Host* host = hostIter->data;

        utility_assert(threadIter);
        GThread* thread = threadIter->data;

//        g_hash_table_replace(scheduler->hostToThreadMap, host, thread);

        hostIter = g_list_next(hostIter);
        threadIter = g_list_next(threadIter);
        if(!threadIter) {
            threadIter = g_list_first(scheduler->workerThreads);
        }
    }
}

static void _scheduler_startHosts(Scheduler* scheduler) {
    if(scheduler->policy->getAssignedHosts) {
        GList* myHosts = scheduler->policy->getAssignedHosts(scheduler->policy);
        if(myHosts) {
            guint nHosts = g_list_length(myHosts);
            message("starting to boot %u hosts", nHosts);
            worker_bootHosts(myHosts);
            message("%u hosts are booted", nHosts);
        }
    }
}

static void _scheduler_stopHosts(Scheduler* scheduler) {
    /* free all applications before freeing any of the hosts since freeing
     * applications may cause close() to get called on sockets which needs
     * other host information. this may cause issues if the hosts are gone.
     *
     * do the following if it turns out we need each worker to free their assigned hosts.
     * i dont think it should be a problem to swap hosts between threads given our current
     * program state context switching, but am not sure about plugins that use other linked libs.
     *
     * **update** it doesnt work. for example, each instance of the tor plugin keeps track of
     * how many hosts it created, and then when that many hosts are freed, it frees openssl
     * structs. so if we let a single thread free everything, we run into issues. */

//    GList* allHosts = g_hash_table_get_values(scheduler->hostIDToHostMap);
    if(scheduler->policy->getAssignedHosts) {
        GList* myHosts = scheduler->policy->getAssignedHosts(scheduler->policy);
        if(myHosts) {
            guint nHosts = g_list_length(myHosts);
            message("starting to shut down %u hosts", nHosts);
            worker_freeHosts(myHosts);
            message("%u hosts are shut down", nHosts);
        }
    }
}

Scheduler* scheduler_new(SchedulerPolicyType policyType, guint nWorkers, gpointer threadUserData,
        guint schedulerSeed, SimulationTime endTime) {
    Scheduler* scheduler = g_new0(Scheduler, 1);
    MAGIC_INIT(scheduler);

    /* global lock */
    g_mutex_init(&(scheduler->globalLock));

    scheduler->startBarrier = countdownlatch_new(nWorkers+1);
    scheduler->finishBarrier = countdownlatch_new(nWorkers+1);
    scheduler->executeEventsBarrier = countdownlatch_new(nWorkers+1);
    scheduler->collectInfoBarrier = countdownlatch_new(nWorkers+1);
    scheduler->prepareRoundBarrier = countdownlatch_new(nWorkers+1);

    scheduler->endTime = endTime;
    scheduler->currentRound.endTime = scheduler->endTime;// default to one single round
    scheduler->currentRound.minNextEventTime = SIMTIME_MAX;

    scheduler->hostIDToHostMap = g_hash_table_new(g_direct_hash, g_direct_equal);

    scheduler->random = random_new(schedulerSeed);

    /* ensure we have sane default modes for the number of workers we are using */
    if(nWorkers == 0) {
        scheduler->policyType = SP_SERIAL_GLOBAL;
    } else if(nWorkers > 0 && policyType == SP_SERIAL_GLOBAL) {
        policyType = SP_PARALLEL_HOST_SINGLE;
    } else {
        scheduler->policyType = policyType;
    }

    /* create the configured policy to handle queues */
    switch(scheduler->policyType) {
        case SP_PARALLEL_HOST_SINGLE: {
            scheduler->policy = schedulerpolicyhostsingle_new();
            break;
        }
        case SP_PARALLEL_THREAD_SINGLE: {
            scheduler->policy = schedulerpolicythreadsingle_new();
            break;
        }
        case SP_PARALLEL_THREAD_PERTHREAD: {
            scheduler->policy = schedulerpolicythreadperthread_new();
            break;
        }
        case SP_PARALLEL_THREAD_PERHOST: {
            scheduler->policy = schedulerpolicythreadperhost_new();
            break;
        }
        case SP_SERIAL_GLOBAL:
        default: {
            scheduler->policy = schedulerpolicyglobalsingle_new();
            break;
        }
    }
    utility_assert(scheduler->policy);

    /* make sure our ref count is set before starting the threads */
    scheduler->referenceCount = 1;

    /* start up threads and create worker storage, each thread will call worker_new,
     * and wait at startBarrier until we are ready to launch */
    for(gint i = 0; i < nWorkers; i++) {
        GString* name = g_string_new(NULL);
        g_string_printf(name, "worker-%i", (i));

        WorkerRunData* runData = g_new0(WorkerRunData, 1);
        runData->userData = threadUserData;
        runData->scheduler = scheduler;
        runData->threadID = i;

        GThread* t = g_thread_new(name->str, (GThreadFunc)worker_run, runData);
        utility_assert(t);
        scheduler->workerThreads = g_list_append(scheduler->workerThreads, t);

        g_string_free(name, TRUE);
    }
    message("main scheduler thread will operate with %u worker threads", nWorkers);

    return scheduler;
}

void scheduler_shutdown(Scheduler* scheduler) {
    MAGIC_ASSERT(scheduler);

    message("scheduler is shutting down now");

    /* this launches delete on all the plugins and should be called before
     * the engine is marked "killed" and workers are destroyed, so that
     * each plug-in is able to destroy/free its virtual nodes properly */
    g_hash_table_destroy(scheduler->hostIDToHostMap);

    /* join and free spawned worker threads */
    guint nWorkers = g_list_length(scheduler->workerThreads);

    message("waiting for %u worker threads to finish", nWorkers);

    /* wait for the threads to finish their cleanup */
    GList* threadItem = scheduler->workerThreads;
    while(threadItem) {
        GThread* t = threadItem->data;
        /* only join the spawned threads, not the main thread */
        if(t != g_thread_self()) {
            /* the join will consume the reference, so unref is not needed */
            g_thread_join(t);
        }
        threadItem = g_list_next(threadItem);
    }
}

static void _scheduler_free(Scheduler* scheduler) {
    MAGIC_ASSERT(scheduler);

    guint nWorkers = g_list_length(scheduler->workerThreads);
    g_list_free(scheduler->workerThreads);

    scheduler->policy->free(scheduler->policy);
    random_free(scheduler->random);

    countdownlatch_free(scheduler->executeEventsBarrier);
    countdownlatch_free(scheduler->collectInfoBarrier);
    countdownlatch_free(scheduler->prepareRoundBarrier);
    countdownlatch_free(scheduler->startBarrier);
    countdownlatch_free(scheduler->finishBarrier);

    g_mutex_clear(&(scheduler->globalLock));

    message("%i worker threads finished", nWorkers);

    MAGIC_CLEAR(scheduler);
    g_free(scheduler);
}

void scheduler_ref(Scheduler* scheduler) {
    MAGIC_ASSERT(scheduler);
    g_mutex_lock(&(scheduler->globalLock));
    scheduler->referenceCount++;
    g_mutex_unlock(&(scheduler->globalLock));
}

void scheduler_unref(Scheduler* scheduler) {
    MAGIC_ASSERT(scheduler);
    g_mutex_lock(&(scheduler->globalLock));
    scheduler->referenceCount--;
    gboolean shouldFree = (scheduler->referenceCount <= 0) ? TRUE : FALSE;
    g_mutex_unlock(&(scheduler->globalLock));
    if(shouldFree) {
        _scheduler_free(scheduler);
    }
}

void scheduler_push(Scheduler* scheduler, Event* event, GQuark senderHostID, GQuark receiverHostID) {
    MAGIC_ASSERT(scheduler);

    SimulationTime eventTime = event_getTime(event);
    if(eventTime > scheduler->endTime) {
        event_unref(event);
        return;
    }

    /* parties involved. sender may be NULL, receiver may not!
     * we MAY NOT OWN the receiver, so do not write to it! */
    Host* sender = scheduler_getHost(scheduler, senderHostID);
    Host* receiver = scheduler_getHost(scheduler, receiverHostID);
    utility_assert(receiver);
    utility_assert(receiver == event_getHost(event));

    /* push to a queue based on the policy */
    scheduler->policy->push(scheduler->policy, event, sender, receiver, scheduler->currentRound.endTime);
}

Event* scheduler_pop(Scheduler* scheduler) {
    MAGIC_ASSERT(scheduler);

    /* this function should block until a non-null event is available for the worker to run.
     * return NULL only to signal the worker thread to quit */

    while(scheduler->isRunning) {
        /* pop from a queue based on the policy */
        Event* nextEvent = scheduler->policy->pop(scheduler->policy, scheduler->currentRound.endTime);

        if(nextEvent != NULL) {
            /* we have an event, let the worker run it */
            return nextEvent;
        } else if(scheduler->policyType == SP_SERIAL_GLOBAL) {
            return NULL;
        } else {
            /* wait for all other worker threads to finish their events too */
            countdownlatch_countDownAwait(scheduler->executeEventsBarrier);

            /* now all threads reached the current round end barrier time.
             * asynchronously collect some stats that the main thread will use. */
            if(scheduler->policy->getNextTime) {
                SimulationTime nextTime = scheduler->policy->getNextTime(scheduler->policy);
                g_mutex_lock(&(scheduler->globalLock));
                scheduler->currentRound.minNextEventTime = MIN(scheduler->currentRound.minNextEventTime, nextTime);
                g_mutex_unlock(&(scheduler->globalLock));
            }

            /* wait for other threads to finish their collect step */
            countdownlatch_countDownAwait(scheduler->collectInfoBarrier);

            /* now wait for main thread to process a barrier update for the next round */
            countdownlatch_countDownAwait(scheduler->prepareRoundBarrier);
        }
    }

    /* scheduler is done, return NULL to stop worker */
    return NULL;
}

void scheduler_addHost(Scheduler* scheduler, Host* host) {
    MAGIC_ASSERT(scheduler);

    /* this function should only be executed during the initActions phase in
     * scheduler_awaitStart, in which we are already holding the globalLock */

    /* save the host */
    GQuark hostID = host_getID(host);
    gpointer hostIDKey = GUINT_TO_POINTER(hostID);
    g_hash_table_replace(scheduler->hostIDToHostMap, hostIDKey, host);

    /* figure out which thread gets the host */
    GThread* randomThread = g_thread_self();
    gdouble nThreads = (gdouble)g_list_length(scheduler->workerThreads);
    if(nThreads > 0) {
        /* choose a random one instead of whichever one happens to be running initActions) */
        gdouble frac = random_nextDouble(scheduler->random);
        guint index = (guint)round(frac * (nThreads-1.0f));
        randomThread = g_list_nth_data(scheduler->workerThreads, index);
        utility_assert(randomThread);
    }

    scheduler->policy->addHost(scheduler->policy, host, randomThread);
}

Host* scheduler_getHost(Scheduler* scheduler, GQuark hostID) {
    MAGIC_ASSERT(scheduler);
    return (Host*) g_hash_table_lookup(scheduler->hostIDToHostMap, GUINT_TO_POINTER((guint)hostID));
}

SchedulerPolicyType scheduler_getPolicy(Scheduler* scheduler) {
    MAGIC_ASSERT(scheduler);
    return scheduler->policyType;
}

gboolean scheduler_isRunning(Scheduler* scheduler) {
    return scheduler->isRunning;
}

void scheduler_awaitStart(Scheduler* scheduler) {
    /* wait until all threads are waiting to start */
    countdownlatch_countDownAwait(scheduler->startBarrier);

    /* each thread will boot their own hosts */
    _scheduler_startHosts(scheduler);

    /* everyone is waiting for the next round to be ready */
    countdownlatch_countDownAwait(scheduler->prepareRoundBarrier);
}

void scheduler_awaitFinish(Scheduler* scheduler) {
    /* each thread will run cleanup on their own hosts */
    g_mutex_lock(&scheduler->globalLock);
    scheduler->isRunning = FALSE;
    g_mutex_unlock(&scheduler->globalLock);

    _scheduler_stopHosts(scheduler);

    /* wait until all threads are waiting to finish */
    countdownlatch_countDownAwait(scheduler->finishBarrier);
}

void scheduler_start(Scheduler* scheduler) {
    g_mutex_lock(&scheduler->globalLock);
    scheduler->isRunning = TRUE;
    g_mutex_unlock(&scheduler->globalLock);

    if(scheduler->policyType != SP_SERIAL_GLOBAL) {
        /* this will cause a worker to execute the locked initialization in awaitStart */
        countdownlatch_countDownAwait(scheduler->startBarrier);
    }
}

void scheduler_continueNextRound(Scheduler* scheduler, SimulationTime windowStart, SimulationTime windowEnd) {
    g_mutex_lock(&scheduler->globalLock);
    scheduler->currentRound.endTime = windowEnd;
    scheduler->currentRound.minNextEventTime = SIMTIME_MAX;
    g_mutex_unlock(&scheduler->globalLock);

    if(scheduler->policyType != SP_SERIAL_GLOBAL) {
        /* workers are waiting for preparation of the next round
         * this will cause them to start running events */
        countdownlatch_countDownAwait(scheduler->prepareRoundBarrier);

        /* workers are running events now, and will wait at executeEventsBarrier
         * when blocked because there are no more events available in the current round */
        countdownlatch_reset(scheduler->prepareRoundBarrier);
    }
}

SimulationTime scheduler_awaitNextRound(Scheduler* scheduler) {
    if(scheduler->policyType != SP_SERIAL_GLOBAL) {
        /* workers will wait here when they are finished with their events */
        countdownlatch_countDownAwait(scheduler->executeEventsBarrier);
        countdownlatch_reset(scheduler->executeEventsBarrier);
        /* then they collect stats and wait here */
        countdownlatch_countDownAwait(scheduler->collectInfoBarrier);
        countdownlatch_reset(scheduler->collectInfoBarrier);
    }

    SimulationTime minNextEventTime = SIMTIME_MAX;
    g_mutex_lock(&scheduler->globalLock);
    minNextEventTime = scheduler->currentRound.minNextEventTime;
    g_mutex_unlock(&scheduler->globalLock);
    return minNextEventTime;
}

void scheduler_finish(Scheduler* scheduler) {
    /* make sure when the workers wake up they know we are done */
    g_mutex_lock(&scheduler->globalLock);
    scheduler->isRunning = FALSE;
    g_mutex_unlock(&scheduler->globalLock);

    if(scheduler->policyType != SP_SERIAL_GLOBAL) {
        /* wake up threads from their waiting for the next round.
         * because isRunning is now false, they will all exit and wait at finishBarrier */
        countdownlatch_countDownAwait(scheduler->prepareRoundBarrier);

        /* wait for them to be ready to finish */
        countdownlatch_countDownAwait(scheduler->finishBarrier);
    }

    g_mutex_lock(&scheduler->globalLock);
    if(g_hash_table_size(scheduler->hostIDToHostMap) > 0) {
        g_hash_table_remove_all(scheduler->hostIDToHostMap);
    }
    g_mutex_unlock(&scheduler->globalLock);
}
