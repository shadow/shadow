/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* thread-level storage structure */
struct _Worker {
    /* our thread and an id that is unique among all threads */
    pthread_t thread;
    guint threadID;

    /* pointer to the object that communicates with the master process */
    Slave* slave;
    /* pointer to the per-slave parallel scheduler object that feeds events to all workers */
    Scheduler* scheduler;

    /* the random source used for all hosts run by this worker
     * the source is seeded by the master random source. */
    Random* random;

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

    ObjectCounter* objectCounts;

    MAGIC_DECLARE;
};

ObjectCounter* globalObjectCounts = NULL;

static Worker* _worker_new(Slave*, guint);
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

gboolean worker_isAlive() {
    return g_private_get(&workerKey) != NULL;
}

static Worker* _worker_new(Slave* slave, guint threadID) {
    /* make sure this isnt called twice on the same thread! */
    utility_assert(!worker_isAlive());

    Worker* worker = g_new0(Worker, 1);
    MAGIC_INIT(worker);

    worker->slave = slave;
    worker->thread = pthread_self();
    worker->threadID = threadID;
    worker->clock.now = SIMTIME_INVALID;
    worker->clock.last = SIMTIME_INVALID;
    worker->clock.barrier = SIMTIME_INVALID;
    worker->objectCounts = objectcounter_new();

    g_private_replace(&workerKey, worker);

    return worker;
}

static void _worker_free(Worker* worker) {
    MAGIC_ASSERT(worker);

    if(worker->objectCounts != NULL) {
        objectcounter_free(worker->objectCounts);
    }

    g_private_set(&workerKey, NULL);

    MAGIC_CLEAR(worker);
    g_free(worker);
}

DNS* worker_getDNS() {
    Worker* worker = _worker_getPrivate();
    return slave_getDNS(worker->slave);
}

Address* worker_resolveIPToAddress(in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    DNS* dns = slave_getDNS(worker->slave);
    return dns_resolveIPToAddress(dns, ip);
}

Address* worker_resolveNameToAddress(const gchar* name) {
    Worker* worker = _worker_getPrivate();
    DNS* dns = slave_getDNS(worker->slave);
    return dns_resolveNameToAddress(dns, name);
}

Topology* worker_getTopology() {
    Worker* worker = _worker_getPrivate();
    return slave_getTopology(worker->slave);
}

Options* worker_getOptions() {
    Worker* worker = _worker_getPrivate();
    return slave_getOptions(worker->slave);
}

/* this is the entry point for worker threads when running in parallel mode,
 * and otherwise is the main event loop when running in serial mode */
gpointer worker_run(WorkerRunData* data) {
    utility_assert(data && data->userData && data->scheduler);

    /* create the worker object for this worker thread */
    Worker* worker = _worker_new((Slave*)data->userData, data->threadID);
    utility_assert(worker_isAlive());

    worker->scheduler = data->scheduler;
    scheduler_ref(worker->scheduler);

    /* wait until the slave is done with initialization */
    scheduler_awaitStart(worker->scheduler);

    /* ask the slave for the next event, blocking until one is available that
     * we are allowed to run. when this returns NULL, we should stop. */
    Event* event = NULL;
    while((event = scheduler_pop(worker->scheduler)) != NULL) {
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

    scheduler_unref(worker->scheduler);

    /* tell that we are done running */
    if(data->notifyDoneRunning) {
        countdownlatch_countDown(data->notifyDoneRunning);
    }

    /* wait for other cleanup to finish */
    if(data->notifyReadyToJoin) {
        countdownlatch_await(data->notifyReadyToJoin);
    }

    /* cleanup is all done */
    message("thread-specific info after cleanup: %s", objectcounter_toString(worker->objectCounts));

    /* synchronize thread join */
    CountDownLatch* notifyJoined = data->notifyJoined;

    /* this is a hack so that we don't free the worker before the scheduler is
     * finished with object cleanup when running in global mode.
     * normally, the if statement would not be necessary and we just free
     * the worker in all cases. */
    if(notifyJoined) {
        _worker_free(worker);
        g_free(data);
    }

    /* now the thread has ended */
    if(notifyJoined) {
        countdownlatch_countDown(notifyJoined);
    }

    /* calling pthread_exit(NULL) is equivalent to returning NULL for spawned threads
     * returning NULL means we don't have to worry about calling pthread_exit on the main thread */
    return NULL;
}

void worker_scheduleTask(Task* task, SimulationTime nanoDelay) {
    utility_assert(task);

    Worker* worker = _worker_getPrivate();

    if(slave_schedulerIsRunning(worker->slave)) {
        utility_assert(worker->clock.now != SIMTIME_INVALID);
        utility_assert(worker->active.host != NULL);
        Event* event = event_new_(task, worker->clock.now + nanoDelay, worker->active.host);
        GQuark hostID = host_getID(worker->active.host);
        scheduler_push(worker->scheduler, event, hostID, hostID);
    }
}

static void _worker_runDeliverPacketTask(Packet* packet, gpointer userData) {
    in_addr_t ip = packet_getDestinationIP(packet);
    NetworkInterface* interface = host_lookupInterface(_worker_getPrivate()->active.host, ip);
    utility_assert(interface != NULL);
    networkinterface_packetArrived(interface, packet);
}

void worker_sendPacket(Packet* packet) {
    utility_assert(packet != NULL);

    /* get our thread-private worker */
    Worker* worker = _worker_getPrivate();
    if(!slave_schedulerIsRunning(worker->slave)) {
        /* the simulation is over, don't bother */
        return;
    }

    in_addr_t srcIP = packet_getSourceIP(packet);
    in_addr_t dstIP = packet_getDestinationIP(packet);

    Address* srcAddress = worker_resolveIPToAddress(srcIP);
    Address* dstAddress = worker_resolveIPToAddress(dstIP);

    if(!srcAddress || !dstAddress) {
        error("unable to schedule packet because of null addresses");
        return;
    }

    /* check if network reliability forces us to 'drop' the packet */
    gdouble reliability = topology_getReliability(worker_getTopology(), srcAddress, dstAddress);
    Random* random = host_getRandom(worker_getActiveHost());
    gdouble chance = random_nextDouble(random);

    /* don't drop control packets with length 0, otherwise congestion
     * control has problems responding to packet loss */
    if(chance <= reliability || packet_getPayloadLength(packet) == 0) {
        /* the sender's packet will make it through, find latency */
        gdouble latency = topology_getLatency(worker_getTopology(), srcAddress, dstAddress);
        SimulationTime delay = (SimulationTime) ceil(latency * SIMTIME_ONE_MILLISECOND);
        SimulationTime deliverTime = worker->clock.now + delay;

        /* TODO this should change for sending to remote slave (on a different machine)
         * this is the only place where tasks are sent between separate hosts */

        Host* srcHost = worker->active.host;
        GQuark srcID = srcHost == NULL ? 0 : host_getID(srcHost);
        GQuark dstID = (GQuark)address_getID(dstAddress);
        Host* dstHost = scheduler_getHost(worker->scheduler, dstID);
        utility_assert(dstHost);

        packet_ref(packet);
        Task* packetTask = task_new((TaskCallbackFunc)_worker_runDeliverPacketTask,
                packet, NULL, (TaskObjectFreeFunc)packet_unref, NULL);
        Event* packetEvent = event_new_(packetTask, deliverTime, dstHost);
        task_unref(packetTask);

        scheduler_push(worker->scheduler, packetEvent, srcID, dstID);

        packet_addDeliveryStatus(packet, PDS_INET_SENT);
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
    if(worker->active.process) {
        process_unref(worker->active.process);
        worker->active.process = NULL;
    }
    if(proc) {
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
    if(worker->active.host != NULL) {
        host_unref(worker->active.host);
        worker->active.host = NULL;
    }

    /* make sure to ref the new host if there is one */
    if(host != NULL) {
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

guint worker_getRawCPUFrequency() {
    Worker* worker = _worker_getPrivate();
    return slave_getRawCPUFrequency(worker->slave);
}

guint32 worker_getNodeBandwidthUp(GQuark nodeID, in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    return slave_getNodeBandwidthUp(worker->slave, nodeID, ip);
}

guint32 worker_getNodeBandwidthDown(GQuark nodeID, in_addr_t ip) {
    Worker* worker = _worker_getPrivate();
    return slave_getNodeBandwidthDown(worker->slave, nodeID, ip);
}

gdouble worker_getLatency(GQuark sourceNodeID, GQuark destinationNodeID) {
    Worker* worker = _worker_getPrivate();
    return slave_getLatency(worker->slave, sourceNodeID, destinationNodeID);
}

gint worker_getThreadID() {
    Worker* worker = _worker_getPrivate();
    return worker->threadID;
}

void worker_updateMinTimeJump(gdouble minPathLatency) {
    Worker* worker = _worker_getPrivate();
    slave_updateMinTimeJump(worker->slave, minPathLatency);
}

void worker_setCurrentTime(SimulationTime time) {
    Worker* worker = _worker_getPrivate();
    worker->clock.now = time;
}

gboolean worker_isFiltered(LogLevel level) {
    return logger_shouldFilter(logger_getDefault(), level);
}

void worker_incrementPluginError() {
    Worker* worker = _worker_getPrivate();
    slave_incrementPluginError(worker->slave);
}

const gchar* worker_getHostsRootPath() {
    Worker* worker = _worker_getPrivate();
    return slave_getHostsRootPath(worker->slave);
}

void worker_countObject(ObjectType otype, CounterType ctype) {
    /* the issue is that the slave thread frees some objects that
     * are created by the worker threads. but the slave thread does
     * not have a worker object. this is only an issue when running
     * with multiple workers. */
    if(worker_isAlive()) {
        Worker* worker = _worker_getPrivate();
        objectcounter_increment(worker->objectCounts, otype, ctype);
    } else {
        /* this is a hack, but we don't want to miss free calls */
        if(!globalObjectCounts) {
            globalObjectCounts = objectcounter_new();
            objectcounter_increment(globalObjectCounts, otype, ctype);
        }
    }
}

void worker_logAndFreeGlobalObjectCounts() {
    if(globalObjectCounts != NULL) {
        message("global state: %s", objectcounter_toString(globalObjectCounts));
        objectcounter_free(globalObjectCounts);
    }
}
