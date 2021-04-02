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

#include "main/core/logger/shadow_logger.h"
#include "main/core/logical_processor.h"
#include "main/core/manager.h"
#include "main/core/scheduler/scheduler.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/support/options.h"
#include "main/core/work/event.h"
#include "main/core/work/task.h"
#include "main/host/affinity.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/packet.h"
#include "main/routing/router.h"
#include "main/routing/topology.h"
#include "main/utility/count_down_latch.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/log_level.h"
#include "support/logger/logger.h"

static void* _worker_run(void* voidWorker);
static void _worker_free(Worker* worker);
static void _worker_freeHostProcesses(Host* host, Worker* worker);
static void _worker_shutdownHost(Host* host, Worker* worker);
static void _workerpool_setLogicalProcessorIdx(WorkerPool* workerpool,
                                               Worker* worker, int cpuId);

struct _Worker {
    WorkerPool* workerPool;

    /* our thread and an id that is unique among all threads */
    pthread_t thread;
    int threadID;
    pid_t nativeThreadID;

    /* Index into workerPool->logicalProcessors */
    int logicalProcessorIdx;

    /* timing information tracked by this worker */
    struct {
        SimulationTime now;
        SimulationTime last;
        SimulationTime barrier;
    } clock;

    /* cached storage of active objects for the event that
     * is currently being processed by the worker */
    struct {
        Host* host;
        Process* process;
    } active;

    SimulationTime bootstrapEndTime;

    ObjectCounter* objectCounts;

    /* Used by the WorkerPool to start the Worker for each task */
    sem_t beginSem;

    MAGIC_DECLARE;
};

struct _WorkerPool {
    /* Unowned pointer to the object that communicates with the controller
     * process */
    Manager* manager;

    /* Unowned pointer to the per-manager parallel scheduler object that feeds
     * events to all workers */
    Scheduler* scheduler;

    /* Number of Worker threads */
    int nWorkers;
    /* Array of size `nWorkers`. */
    Worker** workers;

    /* Tracks completion of the current task */
    CountDownLatch* finishLatch;

    /* Current task being executed by workers */
    WorkerPoolTaskFn taskFn;
    void* taskData;

    /* Whether the worker threads have been joined */
    gboolean joined;

    /* Set of logical processors on which workers run */
    LogicalProcessors* logicalProcessors;

    MAGIC_DECLARE;
};

static Worker* _worker_new(WorkerPool*, int);
static void _worker_free(Worker*);

WorkerPool* workerpool_new(Manager* manager, Scheduler* scheduler, int nWorkers,
                           int nConcurrent) {
    int nLogicalProcessors = 0;
    if (nWorkers == 0 || nConcurrent == 0) {
        // With no concurrency, we still use a single logical processor.
        nLogicalProcessors = 1;
    } else if (nConcurrent < 0 || nConcurrent > nWorkers) {
        // Never makes sense to use more logical processors than workers.
        nLogicalProcessors = nWorkers;
    } else {
        nLogicalProcessors = nConcurrent;
    }

    WorkerPool* pool = g_new(WorkerPool, 1);
    *pool = (WorkerPool){
        .manager = manager,
        .scheduler = scheduler,
        .nWorkers = nWorkers,
        .finishLatch = countdownlatch_new(nWorkers),
        .joined = FALSE,
        .logicalProcessors = lps_new(nLogicalProcessors),
    };
    MAGIC_INIT(pool);

    if (nWorkers == 0) {
        // Create singleton worker object, which will run on this thread.
        pool->workers = g_new(Worker*, 1);
        Worker* worker = _worker_new(pool, -1);
        // The worker runs on the single shadow thread. tid=0 refers to "this"
        // thread.
        worker->nativeThreadID = 0;
        pool->workers[0] = worker;
        _workerpool_setLogicalProcessorIdx(pool, worker, 0);
        return pool;
    }

    pool->workers = g_new(Worker*, nWorkers);
    for (int i = 0; i < nWorkers; ++i) {
        pool->workers[i] = _worker_new(pool, i);
    }

    // Wait for all threads to set their tid
    countdownlatch_await(pool->finishLatch);
    countdownlatch_reset(pool->finishLatch);

    for (int i = 0; i < nWorkers; ++i) {
        Worker* worker = pool->workers[i];
        utility_assert(worker->nativeThreadID > 0);
        int lpi = i % nLogicalProcessors;
        lps_readyPush(pool->logicalProcessors, lpi, worker);
        _workerpool_setLogicalProcessorIdx(pool, worker, lpi);
    }

    return pool;
}

// Find and return a Worker to run the current or next task on `toLpi`. Prefers
// a Worker that last ran on `toLpi`, but if none is available will take one
// from another logical processor.
//
// TODO: Take locality into account when finding another LogicalProcessor to
// migrate from, when needed.
static Worker* _workerpool_getNextWorkerForLogicalProcessorIdx(WorkerPool* pool,
                                                               int toLpi) {
    Worker* nextWorker = lps_popWorkerToRunOn(pool->logicalProcessors, toLpi);
    if (nextWorker) {
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
    utility_assert(pool->taskFn == NULL);

    pool->taskFn = taskFn;
    pool->taskData = data;

    for (int i = 0; i < lps_n(pool->logicalProcessors); ++i) {
        Worker* worker =
            _workerpool_getNextWorkerForLogicalProcessorIdx(pool, i);
        if (worker) {
            MAGIC_ASSERT(worker);
            lps_idleTimerStop(pool->logicalProcessors, i);
            if (sem_post(&worker->beginSem) != 0) {
                error("sem_post: %s", g_strerror(errno));
            }
        } else {
            // There's no more work to do.
            break;
        }
    }
}

void workerpool_joinAll(WorkerPool* pool) {
    MAGIC_ASSERT(pool);
    utility_assert(!pool->joined);

    // Signal threads to exit.
    _workerpool_startTaskFn(pool, NULL, NULL);

    // Not strictly necessary, but could help clarity/debugging.
    workerpool_awaitTaskFn(pool);

#ifdef USE_PERF_TIMERS
    for (int i = 0; i < lps_n(pool->logicalProcessors); ++i) {
        message("Logical Processor %d total idle time was %f seconds", i,
                lps_idleTimerElapsed(pool->logicalProcessors, i));
    }
#endif

    // Join each pthread. (Alternatively we could use pthread_detach on startup)
    for (int i = 0; i < pool->nWorkers; ++i) {
        void* threadRetval;
        int rv = pthread_join(pool->workers[i]->thread, &threadRetval);
        if (rv != 0) {
            error("pthread_join: %s", g_strerror(rv));
        }
        utility_assert(threadRetval == NULL);
    }

    pool->joined = TRUE;
}

void workerpool_free(WorkerPool* pool) {
    MAGIC_ASSERT(pool);
    utility_assert(pool->joined);

    // When there are 0 worker threads, we still have a single worker object.
    int workersToFree = MAX(pool->nWorkers, 1);

    // Free threads.
    for (int i = 0; i < workersToFree; ++i) {
        g_clear_pointer(&pool->workers[i], _worker_free);
    }
    g_clear_pointer(&pool->workers, g_free);
    g_clear_pointer(&pool->finishLatch, countdownlatch_free);

    g_clear_pointer(&pool->logicalProcessors, lps_free);

    MAGIC_CLEAR(pool);
}

void workerpool_startTaskFn(WorkerPool* pool, WorkerPoolTaskFn taskFn,
                            void* taskData) {
    MAGIC_ASSERT(pool);
    // Public interface doesn't support NULL taskFn
    utility_assert(taskFn);
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
    utility_assert(threadId < pool->nWorkers);
    return pool->workers[threadId]->thread;
}

int workerpool_getNWorkers(WorkerPool* pool) {
    MAGIC_ASSERT(pool);
    return pool->nWorkers;
}

static void _workerpool_setLogicalProcessorIdx(WorkerPool* workerPool,
                                               Worker* worker,
                                               int logicalProcessorIdx) {
    MAGIC_ASSERT(workerPool);
    MAGIC_ASSERT(worker);
    utility_assert(logicalProcessorIdx < lps_n(workerPool->logicalProcessors));
    utility_assert(logicalProcessorIdx >= 0);

    int oldCpuId = worker->logicalProcessorIdx >= 0
                       ? lps_cpuId(workerPool->logicalProcessors,
                                   worker->logicalProcessorIdx)
                       : AFFINITY_UNINIT;
    worker->logicalProcessorIdx = logicalProcessorIdx;
    int newCpuId =
        lps_cpuId(workerPool->logicalProcessors, logicalProcessorIdx);

    // Set affinity of the worker thread to match that of the logical processor.
    affinity_setProcessAffinity(worker->nativeThreadID, newCpuId, oldCpuId);
}

static __thread Worker* _threadWorker = NULL;

static Worker* _worker_getPrivate() {
    MAGIC_ASSERT(_threadWorker);
    return _threadWorker;
}

gboolean worker_isAlive() { return _threadWorker != NULL; }

static Worker* _worker_new(WorkerPool* workerPool, int threadID) {
    Worker* worker = g_new0(Worker, 1);
    MAGIC_INIT(worker);

    worker->workerPool = workerPool;
    worker->threadID = threadID;
    worker->clock.now = SIMTIME_INVALID;
    worker->clock.last = SIMTIME_INVALID;
    worker->clock.barrier = SIMTIME_INVALID;
    worker->objectCounts = objectcounter_new();
    worker->logicalProcessorIdx = -1;

    worker->bootstrapEndTime = manager_getBootstrapEndTime(workerPool->manager);

    // Calling thread is the sole worker thread
    if (threadID < 0) {
        _threadWorker = worker;
        return worker;
    }

    sem_init(&worker->beginSem, 0, 0);

    int rv = pthread_create(&worker->thread, NULL, _worker_run, worker);
    if (rv != 0) {
        error("pthread_create: %s", g_strerror(rv));
    }

    GString* name = g_string_new(NULL);
    g_string_printf(name, "worker-%i", threadID);
    rv = pthread_setname_np(worker->thread, name->str);
    if (rv != 0) {
        warning("unable to set name of worker thread to '%s': %s", name->str,
                g_strerror(rv));
    }
    g_string_free(name, TRUE);

    shadow_logger_register(shadow_logger_getDefault(), worker->thread);

    return worker;
}

static void _worker_free(Worker* worker) {
    MAGIC_ASSERT(worker);
    utility_assert(!worker->active.host);
    utility_assert(!worker->active.process);
    utility_assert(worker->objectCounts);

    objectcounter_free(worker->objectCounts);

    _threadWorker = NULL;

    MAGIC_CLEAR(worker);
    g_free(worker);
}

int worker_getAffinity() {
    Worker* worker = _worker_getPrivate();
    return lps_cpuId(worker->workerPool->logicalProcessors,
                     worker->logicalProcessorIdx);
}

DNS* worker_getDNS() {
    Worker* worker = _worker_getPrivate();
    return manager_getDNS(worker->workerPool->manager);
}

Address* worker_resolveIPToAddress(in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    DNS* dns = manager_getDNS(worker->workerPool->manager);
    return dns_resolveIPToAddress(dns, ip);
}

Address* worker_resolveNameToAddress(const gchar* name) {
    Worker* worker = _worker_getPrivate();
    DNS* dns = manager_getDNS(worker->workerPool->manager);
    return dns_resolveNameToAddress(dns, name);
}

Topology* worker_getTopology() {
    Worker* worker = _worker_getPrivate();
    return manager_getTopology(worker->workerPool->manager);
}

Options* worker_getOptions() {
    Worker* worker = _worker_getPrivate();
    return manager_getOptions(worker->workerPool->manager);
}

/* this is the entry point for worker threads when running in parallel mode,
 * and otherwise is the main event loop when running in serial mode */
void* _worker_run(void* voidWorker) {
    Worker* worker = voidWorker;
    MAGIC_ASSERT(worker);
    WorkerPool* workerPool = worker->workerPool;
    MAGIC_ASSERT(workerPool);
    LogicalProcessors* lps = workerPool->logicalProcessors;

    _threadWorker = worker;

    // We can't report any errors here, since parent might not have registered
    // this thread with the logger yet. Parent thread will check the result.
    worker->nativeThreadID = syscall(SYS_gettid);

    // Signal parent thread that we've set the nativeThreadID.
    countdownlatch_countDown(workerPool->finishLatch);

    WorkerPoolTaskFn taskFn = NULL;
    do {
        // Wait for work to do.
        if (sem_wait(&worker->beginSem) != 0) {
            error("sem_wait: %s", g_strerror(errno));
        }
        int lpi = worker->logicalProcessorIdx;

        taskFn = workerPool->taskFn;
        if (taskFn != NULL) {
            taskFn(workerPool->taskData);
        }

        lps_donePush(lps, lpi, worker);

        Worker* nextWorker = _workerpool_getNextWorkerForLogicalProcessorIdx(
            workerPool, worker->logicalProcessorIdx);
        if (nextWorker) {
            // Start running the next worker.
            if (sem_post(&nextWorker->beginSem) != 0) {
                error("sem_post: %s", g_strerror(errno));
            }
        } else {
            // No more workers to run; lpi is now idle.
            lps_idleTimerContinue(workerPool->logicalProcessors, lpi);
        }
        countdownlatch_countDown(workerPool->finishLatch);
    } while (taskFn != NULL);
    debug("Worker finished");
    return NULL;
}

void worker_runEvent(Event* event) {
    Worker* worker = _worker_getPrivate();

    /* update cache, reset clocks */
    worker->clock.now = event_getTime(event);

    /* process the local event */
    event_execute(event);
    event_unref(event);

    /* update times */
    worker->clock.last = worker->clock.now;
    worker->clock.now = SIMTIME_INVALID;
}

void worker_finish(GQueue* hosts) {
    Worker* worker = _worker_getPrivate();

    if (hosts) {
        guint nHosts = g_queue_get_length(hosts);
        message("starting to shut down %u hosts", nHosts);
        g_queue_foreach(hosts, (GFunc)_worker_freeHostProcesses, worker);
        g_queue_foreach(hosts, (GFunc)_worker_shutdownHost, worker);
        message("%u hosts are shut down", nHosts);
    }

    // Flushes any remaining message buffered for this thread.
    shadow_logger_flushRecords(shadow_logger_getDefault(), pthread_self());

    /* cleanup is all done, send object counts to manager */
    manager_storeCounts(worker->workerPool->manager, worker->objectCounts);
}

gboolean worker_scheduleTask(Task* task, SimulationTime nanoDelay) {
    utility_assert(task);

    Worker* worker = _worker_getPrivate();

    if (manager_schedulerIsRunning(worker->workerPool->manager)) {
        utility_assert(worker->clock.now != SIMTIME_INVALID);
        utility_assert(worker->active.host != NULL);

        Host* srcHost = worker->active.host;
        Host* dstHost = srcHost;
        Event* event = event_new_(task, worker->clock.now + nanoDelay, srcHost, dstHost);
        return scheduler_push(worker->workerPool->scheduler, event, srcHost,
                              dstHost);
    } else {
        return FALSE;
    }
}

static void _worker_runDeliverPacketTask(Packet* packet, gpointer userData) {
    in_addr_t ip = packet_getDestinationIP(packet);
    Router* router = host_getUpstreamRouter(_worker_getPrivate()->active.host, ip);
    utility_assert(router != NULL);
    router_enqueue(router, packet);
}

void worker_sendPacket(Packet* packet) {
    utility_assert(packet != NULL);

    /* get our thread-private worker */
    Worker* worker = _worker_getPrivate();
    if (!manager_schedulerIsRunning(worker->workerPool->manager)) {
        /* the simulation is over, don't bother */
        return;
    }

    in_addr_t srcIP = packet_getSourceIP(packet);
    in_addr_t dstIP = packet_getDestinationIP(packet);

    Address* srcAddress = worker_resolveIPToAddress(srcIP);
    Address* dstAddress = worker_resolveIPToAddress(dstIP);

    if (!srcAddress || !dstAddress) {
        error("unable to schedule packet because of null addresses");
        return;
    }

    gboolean bootstrapping = worker_isBootstrapActive();

    /* check if network reliability forces us to 'drop' the packet */
    gdouble reliability = topology_getReliability(worker_getTopology(), srcAddress, dstAddress);
    Random* random = host_getRandom(worker_getActiveHost());
    gdouble chance = random_nextDouble(random);

    /* don't drop control packets with length 0, otherwise congestion
     * control has problems responding to packet loss */
    if (bootstrapping || chance <= reliability || packet_getPayloadLength(packet) == 0) {
        /* the sender's packet will make it through, find latency */
        gdouble latency = topology_getLatency(worker_getTopology(), srcAddress, dstAddress);
        SimulationTime delay = (SimulationTime)ceil(latency * SIMTIME_ONE_MILLISECOND);
        SimulationTime deliverTime = worker->clock.now + delay;

        topology_incrementPathPacketCounter(worker_getTopology(), srcAddress, dstAddress);

        /* TODO this should change for sending to remote manager (on a different machine)
         * this is the only place where tasks are sent between separate hosts */

        Host* srcHost = worker->active.host;
        GQuark dstID = (GQuark)address_getID(dstAddress);
        Host* dstHost = scheduler_getHost(worker->workerPool->scheduler, dstID);
        utility_assert(dstHost);

        packet_addDeliveryStatus(packet, PDS_INET_SENT);

        /* the packetCopy starts with 1 ref, which will be held by the packet task
         * and unreffed after the task is finished executing. */
        Packet* packetCopy = packet_copy(packet);

        Task* packetTask = task_new((TaskCallbackFunc)_worker_runDeliverPacketTask, packetCopy,
                                    NULL, (TaskObjectFreeFunc)packet_unref, NULL);
        Event* packetEvent = event_new_(packetTask, deliverTime, srcHost, dstHost);
        task_unref(packetTask);

        scheduler_push(worker->workerPool->scheduler, packetEvent, srcHost,
                       dstHost);
    } else {
        packet_addDeliveryStatus(packet, PDS_INET_DROPPED);
    }
}

static void _worker_bootHost(Host* host, Worker* worker) {
    worker_setActiveHost(host);
    worker->clock.now = 0;
    host_continueExecutionTimer(host);
    host_boot(host);
    host_stopExecutionTimer(host);
    worker->clock.now = SIMTIME_INVALID;
    worker_setActiveHost(NULL);
}

void worker_bootHosts(GQueue* hosts) {
    Worker* worker = _worker_getPrivate();
    g_queue_foreach(hosts, (GFunc)_worker_bootHost, worker);
}

static void _worker_freeHostProcesses(Host* host, Worker* worker) {
    worker_setActiveHost(host);
    host_continueExecutionTimer(host);
    host_freeAllApplications(host);
    host_stopExecutionTimer(host);
    worker_setActiveHost(NULL);
}

static void _worker_shutdownHost(Host* host, Worker* worker) {
    worker_setActiveHost(host);
    host_shutdown(host);
    worker_setActiveHost(NULL);
    host_unref(host);
}

Process* worker_getActiveProcess() {
    Worker* worker = _worker_getPrivate();
    return worker->active.process;
}

void worker_setActiveProcess(Process* proc) {
    Worker* worker = _worker_getPrivate();
    if (worker->active.process) {
        process_unref(worker->active.process);
        worker->active.process = NULL;
    }
    if (proc) {
        process_ref(proc);
        worker->active.process = proc;
    }
}

Host* worker_getActiveHost() {
    Worker* worker = _worker_getPrivate();
    return worker->active.host;
}

void worker_setActiveHost(Host* host) {
    Worker* worker = _worker_getPrivate();

    /* if we are losing a reference, make sure to update the ref count */
    if (worker->active.host != NULL) {
        host_unref(worker->active.host);
        worker->active.host = NULL;
    }

    /* make sure to ref the new host if there is one */
    if (host != NULL) {
        host_ref(host);
        worker->active.host = host;
    }
}

SimulationTime worker_getCurrentTime() {
    Worker* worker = _worker_getPrivate();
    return worker->clock.now;
}

/* The emulated time starts at January 1st, 2000. This time should be used
 * in any places where time is returned to the application, to handle code
 * that assumes the world is in a relatively recent time. */
EmulatedTime worker_getEmulatedTime() {
    return (EmulatedTime)(worker_getCurrentTime() + EMULATED_TIME_OFFSET);
}

guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    return manager_getNodeBandwidthUp(worker->workerPool->manager, nodeID, ip);
}

guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    return manager_getNodeBandwidthDown(worker->workerPool->manager, nodeID,
                                        ip);
}

gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID) {
    Worker* worker = _worker_getPrivate();
    return manager_getLatency(worker->workerPool->manager, sourceNodeID,
                              destinationNodeID);
}

gint worker_getThreadID() {
    Worker* worker = _worker_getPrivate();
    return worker->threadID;
}

void worker_updateMinTimeJump(gdouble minPathLatency) {
    Worker* worker = _worker_getPrivate();
    manager_updateMinTimeJump(worker->workerPool->manager, minPathLatency);
}

void worker_setCurrentTime(SimulationTime time) {
    Worker* worker = _worker_getPrivate();
    worker->clock.now = time;
}

gboolean worker_isFiltered(LogLevel level) {
    return shadow_logger_shouldFilter(shadow_logger_getDefault(), level);
}

void worker_incrementPluginError() {
    Worker* worker = _worker_getPrivate();
    manager_incrementPluginError(worker->workerPool->manager);
}

void worker_countObject(ObjectType otype, CounterType ctype) {
    /* the issue is that the manager thread frees some objects that
     * are created by the worker threads. but the manager thread does
     * not have a worker object. this is only an issue when running
     * with multiple workers. */
    if (worker_isAlive()) {
        Worker* worker = _worker_getPrivate();
        objectcounter_incrementOne(worker->objectCounts, otype, ctype);
    } else {
        /* has a global lock, so don't do it unless there is no worker object */
        manager_countObject(otype, ctype);
    }
}

gboolean worker_isBootstrapActive() {
    Worker* worker = _worker_getPrivate();

    if (worker->clock.now < worker->bootstrapEndTime) {
        return TRUE;
    } else {
        return FALSE;
    }
}
