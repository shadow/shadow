/*

 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/core/worker.h"

/* thread-level storage structure */
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include "lib/logger/log_level.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/support/definitions.h"
#include "main/host/affinity.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/routing/router.h"
#include "main/utility/count_down_latch.h"
#include "main/utility/utility.h"

static void* _worker_run(void* voidWorker);
static void _worker_freeHostProcesses(Host* host, void* _unused);
static void _worker_shutdownHost(Host* host, void* _unused);
static void _workerpool_setLogicalProcessorIdx(WorkerPool* workerpool, int workerID, int cpuId);

struct _WorkerPool {
    /* Number of Worker threads */
    int nWorkers;

    /* Array of size nWorkers.
     * Used by the WorkerPool to start the Worker for each task.
     *
     * Thread safety: only manipulated via thread-safe
     * methods (e.g. `sem_wait`).
     */
    sem_t* workerBeginSems;
    /* Array of size nWorkers.
     * Thread safety: immutable after initialization from main thread.
     */
    pthread_t* workerThreads;
    /* Array of size nWorkers.
     * Index into workerPool->logicalProcessors, indicating
     * which logicalProcessor the worker last ran on.
     *
     * Thread safety: A given index is only written to in
     * between getting it from lps_popWorkerToRunOn, and
     * allowing that worker to run (by posting to the corresponding
     * `workerBeginSems`. `lps_popWorkerToRunOn` guarantees that
     * only one calling thread should be in that state at one time.
     */
    int* workerLogicalProcessorIdxs;
    /* Array of size nWorkers.
     *
     * Thread safety: Each entry is initialized once, before
     * incrementing `finishLatch` for the first time. It's
     * immutable after that point.
     */
    pid_t* workerNativeThreadIDs;

    /* Tracks completion of the current task */
    CountDownLatch* finishLatch;

    /* Current task being executed by workers.
     * Thread safety: Written-to only by Shadow main thread,
     * while workers aren't running. (i.e. in between `finishLatch`
     * having completed and starting workers again via `workerBeginSems`).
     */
    WorkerPoolTaskFn taskFn;
    void* taskData;

    /* Whether the worker threads have been joined.
     * Thread safety: Written-to only by Shadow main thread,
     * after all worker threads have been joined.
     */
    gboolean joined;

    /* Set of logical processors on which workers run.
     * Thread safety: Initialized before workers are created,
     * and accessed only by thread-safe lps_* methods afterwards.
     */
    LogicalProcessors* logicalProcessors;

    // Array of size lps_n(logicalProcessors) to hold the min event times for
    // all workers. Since only one worker runs on an lp at a time, workers
    // can write to the entry in this array corresponding to their assigned lp
    // without using any locks. Computing the global minimum then only requires
    // a linear scan of O(num_lps) instead of O(num_workers).
    SimulationTime* minEventTimes;

    MAGIC_DECLARE;
};

// Parameters needed to create a new Worker object. Bundled into a struct
// for use with pthread_create.
struct WorkerConstructorParams {
    WorkerPool* pool;
    int threadID;
};

WorkerPool* workerpool_new(int nWorkers, int nParallel) {
    // Should have been ensured earlier by `config_getParallelism`.
    utility_debugAssert(nParallel >= 1);
    utility_debugAssert(nWorkers >= 1);

    // Never makes sense to use more logical processors than workers.
    int nLogicalProcessors = MIN(nParallel, nWorkers);

    WorkerPool* pool = g_new(WorkerPool, 1);
    *pool = (WorkerPool){
        .nWorkers = nWorkers,
        .finishLatch = countdownlatch_new(nWorkers),
        .joined = FALSE,
        .logicalProcessors = lps_new(nLogicalProcessors),
        .minEventTimes = g_new(SimulationTime, nLogicalProcessors),
        .workerBeginSems = g_new0(sem_t, nWorkers),
        .workerThreads = g_new0(pthread_t, nWorkers),
        .workerLogicalProcessorIdxs = g_new0(int, nWorkers),
        .workerNativeThreadIDs = g_new0(pid_t, nWorkers),
    };
    MAGIC_INIT(pool);

    for (int i = 0; i < nLogicalProcessors; ++i) {
        pool->minEventTimes[i] = SIMTIME_MAX;
    }

    for (int threadID = 0; threadID < nWorkers; ++threadID) {
        struct WorkerConstructorParams* info = g_new0(struct WorkerConstructorParams, 1);
        *info = (struct WorkerConstructorParams){
            .pool = pool,
            .threadID = threadID,
        };

        int rv = pthread_create(&pool->workerThreads[threadID], NULL, _worker_run, info);
        if (rv != 0) {
            utility_panic("pthread_create: %s", g_strerror(rv));
        }
    }

    // Wait for all threads to set their tid
    countdownlatch_await(pool->finishLatch);
    countdownlatch_reset(pool->finishLatch);

    for (int workerID = 0; workerID < nWorkers; ++workerID) {
        int lpi = workerID % nLogicalProcessors;
        lps_readyPush(pool->logicalProcessors, lpi, workerID);
        _workerpool_setLogicalProcessorIdx(pool, workerID, lpi);
    }

    return pool;
}

// Find and return a Worker to run the current or next task on `toLpi`. Prefers
// a Worker that last ran on `toLpi`, but if none is available will take one
// from another logical processor.
//
// TODO: Take locality into account when finding another LogicalProcessor to
// migrate from, when needed.
static int _workerpool_getNextWorkerForLogicalProcessorIdx(WorkerPool* pool, int toLpi) {
    int nextWorker = lps_popWorkerToRunOn(pool->logicalProcessors, toLpi);
    if (nextWorker >= 0) {
        _workerpool_setLogicalProcessorIdx(pool, nextWorker, toLpi);
    }
    return nextWorker;
}

// Internal runner. *Does* support NULL `taskFn`, which is used to signal
// cancellation.
void _workerpool_startTaskFn(WorkerPool* pool, WorkerPoolTaskFn taskFn,
                             void* data) {
    MAGIC_ASSERT(pool);

    if (pool->nWorkers == 0) {
        if (taskFn) {
            taskFn(data);
        }
        return;
    }

    // Only supports one task at a time.
    utility_debugAssert(pool->taskFn == NULL);

    pool->taskFn = taskFn;
    pool->taskData = data;

    for (int i = 0; i < lps_n(pool->logicalProcessors); ++i) {
        int workerID = _workerpool_getNextWorkerForLogicalProcessorIdx(pool, i);
        if (workerID >= 0) {
            lps_idleTimerStop(pool->logicalProcessors, i);
            if (sem_post(&pool->workerBeginSems[workerID]) != 0) {
                utility_panic("sem_post: %s", g_strerror(errno));
            }
        } else {
            // There's no more work to do.
            break;
        }
    }
}

void workerpool_joinAll(WorkerPool* pool) {
    MAGIC_ASSERT(pool);
    utility_debugAssert(!pool->joined);

    // Signal threads to exit.
    _workerpool_startTaskFn(pool, NULL, NULL);

    // Not strictly necessary, but could help clarity/debugging.
    workerpool_awaitTaskFn(pool);

#ifdef USE_PERF_TIMERS
    for (int i = 0; i < lps_n(pool->logicalProcessors); ++i) {
        info("Logical Processor %d total idle time was %f seconds", i,
             lps_idleTimerElapsed(pool->logicalProcessors, i));
    }
#endif

    // Join each pthread. (Alternatively we could use pthread_detach on startup)
    for (int i = 0; i < pool->nWorkers; ++i) {
        void* threadRetval;
        int rv = pthread_join(pool->workerThreads[i], &threadRetval);
        if (rv != 0) {
            utility_panic("pthread_join: %s", g_strerror(rv));
        }
        utility_debugAssert(threadRetval == NULL);
    }

    pool->joined = TRUE;
}

void workerpool_free(WorkerPool* pool) {
    MAGIC_ASSERT(pool);
    utility_debugAssert(pool->joined);

    // Free threads.
    for (int i = 0; i < pool->nWorkers; ++i) {
        sem_destroy(&pool->workerBeginSems[i]);
    }
    g_clear_pointer(&pool->workerBeginSems, g_free);
    g_clear_pointer(&pool->workerThreads, g_free);
    g_clear_pointer(&pool->workerLogicalProcessorIdxs, g_free);
    g_clear_pointer(&pool->workerNativeThreadIDs, g_free);
    g_clear_pointer(&pool->finishLatch, countdownlatch_free);

    g_clear_pointer(&pool->logicalProcessors, lps_free);
    g_clear_pointer(&pool->minEventTimes, g_free);

    MAGIC_CLEAR(pool);
}

void workerpool_startTaskFn(WorkerPool* pool, WorkerPoolTaskFn taskFn,
                            void* taskData) {
    MAGIC_ASSERT(pool);
    // Public interface doesn't support NULL taskFn
    utility_debugAssert(taskFn);
    _workerpool_startTaskFn(pool, taskFn, taskData);
}

void workerpool_awaitTaskFn(WorkerPool* pool) {
    MAGIC_ASSERT(pool);
    if (pool->nWorkers == 0) {
        return;
    }
    countdownlatch_await(pool->finishLatch);
    countdownlatch_reset(pool->finishLatch);
    pool->taskFn = NULL;
    pool->taskData = NULL;

    lps_finishTask(pool->logicalProcessors);
}

pthread_t workerpool_getThread(WorkerPool* pool, int threadId) {
    MAGIC_ASSERT(pool);
    utility_debugAssert(threadId < pool->nWorkers);
    return pool->workerThreads[threadId];
}

int workerpool_getNWorkers(WorkerPool* pool) {
    MAGIC_ASSERT(pool);
    return pool->nWorkers;
}

static void _workerpool_setLogicalProcessorIdx(WorkerPool* workerPool, int workerID,
                                               int logicalProcessorIdx) {
    MAGIC_ASSERT(workerPool);
    utility_debugAssert(logicalProcessorIdx < lps_n(workerPool->logicalProcessors));
    utility_debugAssert(logicalProcessorIdx >= 0);

    int oldIdx = workerPool->workerLogicalProcessorIdxs[workerID];
    int oldCpuId = oldIdx >= 0 ? lps_cpuId(workerPool->logicalProcessors, oldIdx) : AFFINITY_UNINIT;
    workerPool->workerLogicalProcessorIdxs[workerID] = logicalProcessorIdx;
    int newCpuId =
        lps_cpuId(workerPool->logicalProcessors, logicalProcessorIdx);

    // Set affinity of the worker thread to match that of the logical processor.
    affinity_setProcessAffinity(workerPool->workerNativeThreadIDs[workerID], newCpuId, oldCpuId);
}

SimulationTime workerpool_getGlobalNextEventTime(WorkerPool* workerPool) {
    MAGIC_ASSERT(workerPool);

    // Compute the min time for next round, and reset for the following round.
    // This is called by a single thread in-between rounds while the workers
    // are idle, so let's not do anything too expensive here.
    SimulationTime minTime = SIMTIME_MAX;

    for (int i = 0; i < lps_n(workerPool->logicalProcessors); ++i) {
        if (workerPool->minEventTimes[i] < minTime) {
            minTime = workerPool->minEventTimes[i];
        }
        workerPool->minEventTimes[i] = SIMTIME_MAX;
    }

    return minTime;
}

/*
void worker_setMinEventTimeNextRound(SimulationTime simtime) {
    // If the event will be executed during *this* round, it should not
    // be considered while computing the start time of the *next* round.
    if (simtime < _worker_getRoundEndTime()) {
        return;
    }

    // No need to lock: worker is the only one running on lpi right now.
    WorkerPool* pool = _worker_pool();
    int lpi = pool->workerLogicalProcessorIdxs[worker_threadID()];
    if (simtime < pool->minEventTimes[lpi]) {
        pool->minEventTimes[lpi] = simtime;
    }
}
*/

/*
int worker_getAffinity() {
    WorkerPool* pool = _worker_pool();
    return lps_cpuId(pool->logicalProcessors, pool->workerLogicalProcessorIdxs[worker_threadID()]);
}
*/

/* this is the entry point for worker threads when running in parallel mode,
 * and otherwise is the main event loop when running in serial mode */
void* _worker_run(void* voidWorkerThreadInfo) {
    utility_panic("POOL");

    /*
    // WorkerPool, owned by the Shadow main thread (Initialized below).
    // See thread-safety comments in struct _WorkerPool definition.
    WorkerPool* workerPool = NULL;

    // ID of this thread, which is also an index into workerPool arrays.
    int threadID = -1;

    // Take contents of `info` and free it.
    {
        struct WorkerConstructorParams* info = voidWorkerThreadInfo;
        workerPool = info->pool;
        threadID = info->threadID;
        g_clear_pointer(&info, g_free);
    }

    // Set thread name
    {
        int rv = pthread_setname_np(pthread_self(), "shadow-worker");
        if (rv != 0) {
            warning("unable to set name of worker thread: %s", g_strerror(rv));
        }
    }

    LogicalProcessors* lps = workerPool->logicalProcessors;

    // Initialize this thread's 'rows' in `workerPool`.
    sem_init(&workerPool->workerBeginSems[threadID], 0, 0);
    workerPool->workerLogicalProcessorIdxs[threadID] = -1;
    workerPool->workerNativeThreadIDs[threadID] = syscall(SYS_gettid);

    // Create the thread-local Worker object.
    worker_newForThisThread(workerPool, threadID);

    // Signal parent thread that we've set the nativeThreadID.
    countdownlatch_countDown(workerPool->finishLatch);

    WorkerPoolTaskFn taskFn = NULL;
    do {
        // Wait for work to do.
        if (sem_wait(&workerPool->workerBeginSems[threadID]) != 0) {
            utility_panic("sem_wait: %s", g_strerror(errno));
        }

        taskFn = workerPool->taskFn;
        if (taskFn != NULL) {
            taskFn(workerPool->taskData);
        }

        int lpi = workerPool->workerLogicalProcessorIdxs[threadID];
        lps_donePush(lps, lpi, threadID);

        int nextWorkerID = _workerpool_getNextWorkerForLogicalProcessorIdx(workerPool, lpi);
        if (nextWorkerID >= 0) {
            // Start running the next worker.
            if (sem_post(&workerPool->workerBeginSems[nextWorkerID]) != 0) {
                utility_panic("sem_post: %s", g_strerror(errno));
            }
        } else {
            // No more workers to run; lpi is now idle.
            lps_idleTimerContinue(workerPool->logicalProcessors, lpi);
        }
        countdownlatch_countDown(workerPool->finishLatch);
    } while (taskFn != NULL);
    trace("Worker finished");

    return NULL;
    */
}

void worker_finish(GQueue* hosts, SimulationTime time) {
    worker_setCurrentEmulatedTime(emutime_add_simtime(EMUTIME_SIMULATION_START, time));

    if (hosts) {
        guint nHosts = g_queue_get_length(hosts);
        info("starting to shut down %u hosts", nHosts);
        g_queue_foreach(hosts, (GFunc)_worker_freeHostProcesses, NULL);
        g_queue_foreach(hosts, (GFunc)_worker_shutdownHost, NULL);
        info("%u hosts are shut down", nHosts);
    }

    worker_clearCurrentTime();

    /* send object counts to global counters */
    worker_addToGlobalAllocCounters(_worker_objectAllocCounter(), _worker_objectDeallocCounter());

    /* send syscall counts to global counter */
    worker_addToGlobalSyscallCounter(_worker_syscallCounter());
}

EmulatedTime worker_maxEventRunaheadTime(Host* host) {
    utility_debugAssert(host);
    EmulatedTime max = emutime_add_simtime(EMUTIME_SIMULATION_START, _worker_getRoundEndTime());

    EmulatedTime nextEventTime = host_nextEventTime(host);
    if (nextEventTime != 0) {
        max = MIN(max, nextEventTime);
    }

    return max;
}

static void _worker_runDeliverPacketTask(Host* host, gpointer voidPacket, gpointer userData) {
    Packet* packet = voidPacket;
    in_addr_t ip = packet_getDestinationIP(packet);
    Router* router = host_getUpstreamRouter(host, ip);
    utility_debugAssert(router != NULL);
    router_enqueue(router, host, packet);
}

void worker_sendPacket(Host* srcHost, Packet* packet) {
    utility_debugAssert(packet != NULL);

    if (worker_isSimCompleted()) {
        /* the simulation is over, don't bother */
        return;
    }

    in_addr_t srcIP = packet_getSourceIP(packet);
    in_addr_t dstIP = packet_getDestinationIP(packet);

    const Address* dstAddress = worker_resolveIPToAddress(dstIP);

    if (!dstAddress) {
        utility_panic("unable to schedule packet because of null address");
        return;
    }

    gboolean bootstrapping = worker_isBootstrapActive();

    /* check if network reliability forces us to 'drop' the packet */
    gdouble reliability = worker_getReliability(srcIP, dstIP);

    Random* random = host_getRandom(srcHost);
    gdouble chance = random_nextDouble(random);

    /* don't drop control packets with length 0, otherwise congestion
     * control has problems responding to packet loss */
    if (bootstrapping || chance <= reliability || packet_getPayloadSize(packet) == 0) {
        /* the sender's packet will make it through, find latency */
        SimulationTime delay = worker_getLatency(srcIP, dstIP);
        worker_updateLowestUsedLatency(delay);
        SimulationTime deliverTime = worker_getCurrentSimulationTime() + delay;

        worker_incrementPacketCount(srcIP, dstIP);

        /* TODO this should change for sending to remote manager (on a different machine)
         * this is the only place where tasks are sent between separate hosts */

        GQuark dstHostID = (GQuark)address_getID(dstAddress);

        packet_addDeliveryStatus(packet, PDS_INET_SENT);

        /* the packetCopy starts with 1 ref, which will be held by the packet task
         * and unreffed after the task is finished executing. */
        Packet* packetCopy = packet_copy(packet);

        /* Safe to use the "unbound" constructor here, since there are no other references
         * to `packetCopy`.
         */
        TaskRef* packetTask = taskref_new_unbound(
            _worker_runDeliverPacketTask, packetCopy, NULL, (TaskObjectFreeFunc)packet_unref, NULL);

        Event* packetEvent = event_new(packetTask, deliverTime, srcHost, dstHostID);

        taskref_drop(packetTask);

        SimulationTime roundEndTime = _worker_getRoundEndTime();

        // delay the packet until the next round
        if (deliverTime < roundEndTime) {
            event_setTime(packetEvent, roundEndTime);
        }

        // we may have sent this packet after the destination host finished running the current
        // round and calculated its min event time, so we put this in our min event time instead
        worker_setMinEventTimeNextRound(event_getTime(packetEvent));

        worker_pushToHost(dstHostID, packetEvent);
    } else {
        packet_addDeliveryStatus(packet, PDS_INET_DROPPED);
    }
}

static void _worker_bootHost(Host* host, void* _unused) {
    worker_setActiveHost(host);
    worker_setCurrentEmulatedTime(EMUTIME_SIMULATION_START);
    host_continueExecutionTimer(host);
    host_boot(host);
    host_stopExecutionTimer(host);
    worker_clearCurrentTime();
    worker_setActiveHost(NULL);
}

void worker_bootHosts(GQueue* hosts) { g_queue_foreach(hosts, (GFunc)_worker_bootHost, NULL); }

static void _worker_freeHostProcesses(Host* host, void* _unused) {
    worker_setActiveHost(host);
    host_continueExecutionTimer(host);
    host_freeAllApplications(host);
    host_stopExecutionTimer(host);
    worker_setActiveHost(NULL);
}

static void _worker_shutdownHost(Host* host, void* _unused) {
    worker_setActiveHost(host);
    host_shutdown(host);
    worker_setActiveHost(NULL);
    host_unref(host);
}
