/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* thread-level storage structure */
struct _Worker {
    /* our thread and an id that is unique among all threads */
    GThread* thread;
    guint threadID;

    /* pointer to the object that communicates with the master process */
    Slave* slave;
    /* pointer to the per-slave parallel scheduler object that feeds events to all workers */
    Scheduler* scheduler;

    /* the random source used for all hosts run by this worker
     * the source is seeded by the master random source. */
    Random* random;

    /* all plugin programs that have been loaded by this worker */
    GHashTable* privatePrograms;

    /* timing information tracked by this worker */
    struct {
        SimulationTime now;
        SimulationTime last;
        SimulationTime barrier;
    } clock;

    /* cached storage of active objects for the event that
     * is currently being processed by the worker */
    struct {
        Event* event;
        Host* host;
        Program* program;
        Process* process;
        Thread* thread;
    } active;

    MAGIC_DECLARE;
};

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
    worker->thread = g_thread_self();
    worker->threadID = threadID;
    worker->clock.now = SIMTIME_INVALID;
    worker->clock.last = SIMTIME_INVALID;
    worker->clock.barrier = SIMTIME_INVALID;

    /* each worker needs a private copy of each plug-in library */
    worker->privatePrograms = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)program_free);

    g_private_replace(&workerKey, worker);

    return worker;
}

static void _worker_free(Worker* worker) {
    MAGIC_ASSERT(worker);

    /* calls the destroy functions we specified in g_hash_table_new_full */
    g_hash_table_destroy(worker->privatePrograms);

    MAGIC_CLEAR(worker);
    g_private_set(&workerKey, NULL);
    g_free(worker);
}

DNS* worker_getDNS() {
    Worker* worker = _worker_getPrivate();
    return slave_getDNS(worker->slave);
}

Topology* worker_getTopology() {
    Worker* worker = _worker_getPrivate();
    return slave_getTopology(worker->slave);
}

Configuration* worker_getConfig() {
    Worker* worker = _worker_getPrivate();
    return slave_getConfig(worker->slave);
}

void worker_setKillTime(SimulationTime endTime) {
    Worker* worker = _worker_getPrivate();
    slave_setKillTime(worker->slave, endTime);
}

Program* worker_getPrivateProgram(GQuark pluginID) {
    /* worker has a private plug-in for each plugin ID */
    Worker* worker = _worker_getPrivate();
    Program* privateProg = g_hash_table_lookup(worker->privatePrograms, &pluginID);
    if(!privateProg) {
        /* plug-in has yet to be loaded by this worker. do that now. this call
         * will copy the plug-in library to the temporary directory, and open
         * that so each thread can execute in its own memory space.
         */
        Program* prog = slave_getProgram(worker->slave, pluginID);
        privateProg = program_getTemporaryCopy(prog);
        g_hash_table_replace(worker->privatePrograms, program_getID(privateProg), privateProg);
    }

    debug("worker %u using plug-in at %p", worker->threadID, privateProg);

    return privateProg;
}

static void _worker_processEvent(Worker* worker, Event* event) {
    utility_assert(event);

    /* update cache, reset clocks */
    worker->active.event = event;
    worker->active.host = shadowevent_getNode(event);
    worker->clock.now = shadowevent_getTime(event);

    /* lock the node */
    host_lock(worker->active.host);

//    /* make sure we don't jump backward in time */
//    // FIXME I think this logic should go in popEvent and be host-specific
//    worker->clock.now = shadowevent_getTime(worker->active.event);
//    if(worker->clock.last != SIMTIME_INVALID) {
//        utility_assert(worker->clock.now >= worker->clock.last);
//    }

    /* do the local task */
    gboolean isComplete = shadowevent_run(worker->active.event);

    /* update times */
    worker->clock.last = worker->clock.now;
    worker->clock.now = SIMTIME_INVALID;

    /* finished event can now be destroyed */
    if(isComplete) {
        shadowevent_free(worker->active.event);
    }

    /* unlock, clear cache */
    host_unlock(worker->active.host);
    worker->active.host = NULL;
    worker->active.event = NULL;
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
        _worker_processEvent(worker, event);
    }

    /* this will free the host data that we have been managing */
    scheduler_awaitFinish(worker->scheduler);

    scheduler_unref(worker->scheduler);

    _worker_free(worker);
    g_free(data);

    /* calling g_thread_exit(NULL) is equivalent to returning NULL for spawned threads
     * returning NULL means we don't have to worry about calling g_thread_exit on the main thread */
    return NULL;
}

void worker_scheduleEvent(Event* event, SimulationTime nanoDelay, GQuark receiverHostID) {
    /* TODO create accessors, or better yet refactor the work to event class */
    utility_assert(event);

    /* get our thread-private worker */
    Worker* worker = _worker_getPrivate();

    if(!slave_schedulerIsRunning(worker->slave)) {
        /* we are not going to execute any more events, free it and return */
        shadowevent_free(event);
        return;
    } else {
        /* engine is alive and well, assert accurate worker clock */
        utility_assert(worker->clock.now != SIMTIME_INVALID);
    }

    /* parties involved. sender may be NULL, receiver may not! */
    GQuark senderHostID = worker->active.host == NULL ? 0 : host_getID(worker->active.host);
    if(receiverHostID == 0) {
        receiverHostID = senderHostID;
    }
    utility_assert(receiverHostID > 0);

    /* update the event with the time that it should execute */
    shadowevent_setTime(event, worker->clock.now + nanoDelay);

    /* finally, schedule it */
    scheduler_push(worker->scheduler, event, senderHostID, receiverHostID);
}

void worker_schedulePacket(Packet* packet) {
    /* get our thread-private worker */
    Worker* worker = _worker_getPrivate();
    if(!slave_schedulerIsRunning(worker->slave)) {
        /* the simulation is over, don't bother */
        return;
    }

    in_addr_t srcIP = packet_getSourceIP(packet);
    in_addr_t dstIP = packet_getDestinationIP(packet);

    Address* srcAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32) srcIP);
    Address* dstAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32) dstIP);

    if(!srcAddress || !dstAddress) {
        error("unable to schedule packet because of null addresses");
        return;
    }

    /* check if network reliability forces us to 'drop' the packet */
    gdouble reliability = topology_getReliability(worker_getTopology(), srcAddress, dstAddress);
    Random* random = host_getRandom(worker_getCurrentHost());
    gdouble chance = random_nextDouble(random);

    /* don't drop control packets with length 0, otherwise congestion
     * control has problems responding to packet loss */
    if(chance <= reliability || packet_getPayloadLength(packet) == 0) {
        /* the sender's packet will make it through, find latency */
        gdouble latency = topology_getLatency(worker_getTopology(), srcAddress, dstAddress);
        SimulationTime delay = (SimulationTime) ceil(latency * SIMTIME_ONE_MILLISECOND);

        PacketArrivedEvent* event = packetarrived_new(packet);
        worker_scheduleEvent((Event*)event, delay, (GQuark)address_getID(dstAddress));

        packet_addDeliveryStatus(packet, PDS_INET_SENT);
    } else {
        packet_addDeliveryStatus(packet, PDS_INET_DROPPED);
    }
}

Host* worker_getCurrentHost() {
    Worker* worker = _worker_getPrivate();
    return worker->active.host;
}

void worker_freeHosts(GList* hosts) {
    Worker* worker = _worker_getPrivate();
    GList* item = hosts;
    while(item) {
        Host* host = item->data;
        worker->active.host = host;
        host_freeAllApplications(host);
        worker->active.host = NULL;
        item = g_list_next(item);
    }
    item = hosts;
    while(item) {
        Host* host = item->data;
        worker->active.host = host;
        host_free(host);
        worker->active.host = NULL;
        item = g_list_next(item);
    }
}

Thread* worker_getActiveThread() {
    Worker* worker = _worker_getPrivate();
    return worker->active.thread;
}

void worker_setActiveThread(Thread* thread) {
    Worker* worker = _worker_getPrivate();
    if(worker->active.thread) {
        thread_unref(worker->active.thread);
        worker->active.thread = NULL;
    }
    if(thread) {
        thread_ref(thread);
        worker->active.thread = thread;
    }
}

SimulationTime worker_getCurrentTime() {
    Worker* worker = _worker_getPrivate();
    return worker->clock.now;
}

guint worker_getRawCPUFrequency() {
    Worker* worker = _worker_getPrivate();
    return slave_getRawCPUFrequency(worker->slave);
}

gdouble worker_nextRandomDouble() {
    Worker* worker = _worker_getPrivate();
    return slave_nextRandomDouble(worker->slave);
}

gint worker_nextRandomInt() {
    Worker* worker = _worker_getPrivate();
    return slave_nextRandomInt(worker->slave);
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

void worker_addHost(Host* host) {
    Worker* worker = _worker_getPrivate();
    scheduler_addHost(worker->scheduler, host);
}

gint worker_getThreadID() {
    Worker* worker = _worker_getPrivate();
    return worker->threadID;
}

void worker_storeProgram(Program* prog) {
    Worker* worker = _worker_getPrivate();
    slave_storeProgram(worker->slave, prog);
}

Program* worker_getProgram(GQuark pluginID) {
    Worker* worker = _worker_getPrivate();
    return slave_getProgram(worker->slave, pluginID);
}

void worker_setTopology(Topology* topology) {
    Worker* worker = _worker_getPrivate();
    return slave_setTopology(worker->slave, topology);
}

GTimer* worker_getRunTimer() {
    Worker* worker = _worker_getPrivate();
    return slave_getRunTimer(worker->slave);
}

void worker_updateMinTimeJump(gdouble minPathLatency) {
    Worker* worker = _worker_getPrivate();
    slave_updateMinTimeJump(worker->slave, minPathLatency);
}

void worker_heartbeat() {
    Worker* worker = _worker_getPrivate();
    slave_heartbeat(worker->slave, worker->clock.now);
}

void worker_setCurrentTime(SimulationTime time) {
    Worker* worker = _worker_getPrivate();
    worker->clock.now = time;
}

gboolean worker_isFiltered(GLogLevelFlags level) {
    Worker* worker = worker_isAlive() ? _worker_getPrivate() : NULL;

    if(worker) {
        /* check the local node log level first */
        gboolean isNodeLevelSet = FALSE;
        Host* currentHost = worker_getCurrentHost();
        if(worker->active.host) {
            GLogLevelFlags nodeLevel = host_getLogLevel(currentHost);
            if(nodeLevel) {
                isNodeLevelSet = TRUE;
                if(level > nodeLevel) {
                    return TRUE;
                }
            }
        }

        /* only check the global config if the node didnt have a local setting */
        if(!isNodeLevelSet) {
            Configuration* c = slave_getConfig(worker->slave);
            if(c && (level > configuration_getLogLevel(c))) {
                return TRUE;
            }
        }
    }

    return FALSE;
}
