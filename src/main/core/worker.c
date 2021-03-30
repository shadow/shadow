/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/* thread-level storage structure */
#include <glib.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>

#include "main/bindings/c/bindings.h"
#include "main/core/logger/shadow_logger.h"
#include "main/core/manager.h"
#include "main/core/scheduler/scheduler.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/support/options.h"
#include "main/core/work/event.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
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

struct _Worker {
    /* our thread and an id that is unique among all threads */
    pthread_t thread;
    guint threadID;

    /* affinity of the worker. */
    int cpu_num_affinity;

    /* pointer to the object that communicates with the controller process */
    Manager* manager;
    /* pointer to the per-manager parallel scheduler object that feeds events to all workers */
    Scheduler* scheduler;

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

    // A counter for objects allocated by this worker.
    Counter* object_alloc_counter;
    // A counter for objects deallocated by this worker.
    Counter* object_dealloc_counter;

    // A counter for all syscalls made by processes freed by this worker.
    Counter* syscall_counter;

    MAGIC_DECLARE;
};

static Worker* _worker_new(Manager*, guint);
static void _worker_free(Worker*);

/* holds a thread-private key that each thread references to get a private
 * instance of a worker object */
static GPrivate workerKey = G_PRIVATE_INIT((GDestroyNotify)_worker_free);

static Worker* _worker_getPrivate() {
    /* get current thread's private worker object */
    Worker* worker = g_private_get(&workerKey);
    MAGIC_ASSERT(worker);
    return worker;
}

gboolean worker_isAlive() { return g_private_get(&workerKey) != NULL; }

static Worker* _worker_new(Manager* manager, guint threadID) {
    /* make sure this isnt called twice on the same thread! */
    utility_assert(!worker_isAlive());

    Worker* worker = g_new0(Worker, 1);
    MAGIC_INIT(worker);

    worker->cpu_num_affinity = AFFINITY_UNINIT;
    worker->manager = manager;
    worker->thread = pthread_self();
    worker->threadID = threadID;
    worker->clock.now = SIMTIME_INVALID;
    worker->clock.last = SIMTIME_INVALID;
    worker->clock.barrier = SIMTIME_INVALID;
    worker->objectCounts = objectcounter_new();

    worker->bootstrapEndTime = manager_getBootstrapEndTime(worker->manager);

    g_private_replace(&workerKey, worker);

    return worker;
}

static void _worker_free(Worker* worker) {
    MAGIC_ASSERT(worker);

    if (worker->syscall_counter) {
        counter_free(worker->syscall_counter);
    }

    if (worker->object_alloc_counter) {
        counter_free(worker->object_alloc_counter);
    }

    if (worker->object_dealloc_counter) {
        counter_free(worker->object_dealloc_counter);
    }

    if (worker->objectCounts != NULL) {
        objectcounter_free(worker->objectCounts);
    }

    g_private_set(&workerKey, NULL);

    MAGIC_CLEAR(worker);
    g_free(worker);
}

int worker_getAffinity() {
    Worker* worker = _worker_getPrivate();
    return worker->cpu_num_affinity;
}

DNS* worker_getDNS() {
    Worker* worker = _worker_getPrivate();
    return manager_getDNS(worker->manager);
}

Address* worker_resolveIPToAddress(in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    DNS* dns = manager_getDNS(worker->manager);
    return dns_resolveIPToAddress(dns, ip);
}

Address* worker_resolveNameToAddress(const gchar* name) {
    Worker* worker = _worker_getPrivate();
    DNS* dns = manager_getDNS(worker->manager);
    return dns_resolveNameToAddress(dns, name);
}

Topology* worker_getTopology() {
    Worker* worker = _worker_getPrivate();
    return manager_getTopology(worker->manager);
}

Options* worker_getOptions() {
    Worker* worker = _worker_getPrivate();
    return manager_getOptions(worker->manager);
}

static void _worker_setAffinity(Worker* worker) {
    MAGIC_ASSERT(worker);
    int good_cpu_num = affinity_getGoodWorkerAffinity(worker->threadID);
    worker->cpu_num_affinity =
        affinity_setThisProcessAffinity(good_cpu_num, worker->cpu_num_affinity);
}

/* this is the entry point for worker threads when running in parallel mode,
 * and otherwise is the main event loop when running in serial mode */
gpointer worker_run(WorkerRunData* data) {
    utility_assert(data && data->userData && data->scheduler);

    /* create the worker object for this worker thread */
    Worker* worker = _worker_new((Manager*)data->userData, data->threadID);
    utility_assert(worker_isAlive());

    _worker_setAffinity(worker);

    worker->scheduler = data->scheduler;
    scheduler_ref(worker->scheduler);

    /* wait until the manager is done with initialization */
    scheduler_awaitStart(worker->scheduler);

    /* ask the manager for the next event, blocking until one is available that
     * we are allowed to run. when this returns NULL, we should stop. */
    Event* event = NULL;
    while ((event = scheduler_pop(worker->scheduler)) != NULL) {
        /* update cache, reset clocks */
        worker->clock.now = event_getTime(event);

        /* process the local event */
        event_execute(event);
        event_unref(event);

        /* update times */
        worker->clock.last = worker->clock.now;
        worker->clock.now = SIMTIME_INVALID;
    }

    /* this will free the host data that we have been managing */
    scheduler_awaitFinish(worker->scheduler);

    // Flushes any remaining message buffered for this thread.
    shadow_logger_flushRecords(shadow_logger_getDefault(), pthread_self());

    scheduler_unref(worker->scheduler);

    /* tell that we are done running */
    if (data->notifyDoneRunning) {
        countdownlatch_countDown(data->notifyDoneRunning);
    }

    /* wait for other cleanup to finish */
    if (data->notifyReadyToJoin) {
        countdownlatch_await(data->notifyReadyToJoin);
    }

    /* cleanup is all done, send counters to manager */
    manager_storeCounts(worker->manager, worker->objectCounts);

    // Send object counts to manager
    if (worker->object_alloc_counter) {
        manager_add_alloc_object_counts(worker->manager, worker->object_alloc_counter);
    }
    if (worker->object_dealloc_counter) {
        manager_add_dealloc_object_counts(worker->manager, worker->object_dealloc_counter);
    }
    // Send syscall counts to manager
    if (worker->syscall_counter) {
        manager_add_syscall_counts(worker->manager, worker->syscall_counter);
    }

    /* synchronize thread join */
    CountDownLatch* notifyJoined = data->notifyJoined;

    /* this is a hack so that we don't free the worker before the scheduler is
     * finished with object cleanup when running in global mode.
     * normally, the if statement would not be necessary and we just free
     * the worker in all cases. */
    if (notifyJoined) {
        _worker_free(worker);
        g_free(data);
    }

    /* now the thread has ended */
    if (notifyJoined) {
        countdownlatch_countDown(notifyJoined);
    }

    /* calling pthread_exit(NULL) is equivalent to returning NULL for spawned threads
     * returning NULL means we don't have to worry about calling pthread_exit on the main thread */
    return NULL;
}

gboolean worker_scheduleTask(Task* task, SimulationTime nanoDelay) {
    utility_assert(task);

    Worker* worker = _worker_getPrivate();

    if (manager_schedulerIsRunning(worker->manager)) {
        utility_assert(worker->clock.now != SIMTIME_INVALID);
        utility_assert(worker->active.host != NULL);

        Host* srcHost = worker->active.host;
        Host* dstHost = srcHost;
        Event* event = event_new_(task, worker->clock.now + nanoDelay, srcHost, dstHost);
        return scheduler_push(worker->scheduler, event, srcHost, dstHost);
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
    if (!manager_schedulerIsRunning(worker->manager)) {
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
        Host* dstHost = scheduler_getHost(worker->scheduler, dstID);
        utility_assert(dstHost);

        packet_addDeliveryStatus(packet, PDS_INET_SENT);

        /* the packetCopy starts with 1 ref, which will be held by the packet task
         * and unreffed after the task is finished executing. */
        Packet* packetCopy = packet_copy(packet);

        Task* packetTask = task_new((TaskCallbackFunc)_worker_runDeliverPacketTask, packetCopy,
                                    NULL, (TaskObjectFreeFunc)packet_unref, NULL);
        Event* packetEvent = event_new_(packetTask, deliverTime, srcHost, dstHost);
        task_unref(packetTask);

        scheduler_push(worker->scheduler, packetEvent, srcHost, dstHost);
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

void worker_freeHosts(GQueue* hosts) {
    Worker* worker = _worker_getPrivate();
    g_queue_foreach(hosts, (GFunc)_worker_freeHostProcesses, worker);
    g_queue_foreach(hosts, (GFunc)_worker_shutdownHost, worker);
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
    return manager_getNodeBandwidthUp(worker->manager, nodeID, ip);
}

guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    return manager_getNodeBandwidthDown(worker->manager, nodeID, ip);
}

gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID) {
    Worker* worker = _worker_getPrivate();
    return manager_getLatency(worker->manager, sourceNodeID, destinationNodeID);
}

gint worker_getThreadID() {
    Worker* worker = _worker_getPrivate();
    return worker->threadID;
}

void worker_updateMinTimeJump(gdouble minPathLatency) {
    Worker* worker = _worker_getPrivate();
    manager_updateMinTimeJump(worker->manager, minPathLatency);
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
    manager_incrementPluginError(worker->manager);
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

/* COUNTER WARNING:
 * the issue is that the manager thread frees some objects that
 * are created by the worker threads. but the manager thread does
 * not have a worker object. this is only an issue when running
 * with multiple workers. */

void __worker_increment_object_alloc_counter(const char* object_name) {
    // See COUNTER WARNING above.
    if (worker_isAlive()) {
        Worker* worker = _worker_getPrivate();
        if (!worker->object_alloc_counter) {
            worker->object_alloc_counter = counter_new();
        }
        counter_add_value(worker->object_alloc_counter, object_name, 1);
    } else {
        /* has a global lock, so don't do it unless there is no worker object */
        manager_increment_object_alloc_counter_global(object_name);
    }
}

void __worker_increment_object_dealloc_counter(const char* object_name) {
    // See COUNTER WARNING above.
    if (worker_isAlive()) {
        Worker* worker = _worker_getPrivate();
        if (!worker->object_dealloc_counter) {
            worker->object_dealloc_counter = counter_new();
        }
        counter_add_value(worker->object_dealloc_counter, object_name, 1);
    } else {
        /* has a global lock, so don't do it unless there is no worker object */
        manager_increment_object_dealloc_counter_global(object_name);
    }
}

void worker_add_syscall_counts(Counter* syscall_counts) {
    // See COUNTER WARNING above.
    if (worker_isAlive()) {
        Worker* worker = _worker_getPrivate();
        // This is created on the fly, so that if we did not enable counting mode
        // then we don't need to create the counter object.
        if (!worker->syscall_counter) {
            worker->syscall_counter = counter_new();
        }
        counter_add_counter(worker->syscall_counter, syscall_counts);
    } else {
        /* has a global lock, so don't do it unless there is no worker object */
        manager_add_syscall_counts_global(syscall_counts);
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
