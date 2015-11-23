/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

/* thread-level storage structure */
struct _Worker {
    gint thread_id;

    Slave* slave;

    /* if single threaded, use this global event priority queue */
    EventQueue* serialEventQueue;

    SimulationTime clock_now;
    SimulationTime clock_last;
    SimulationTime clock_barrier;

    Random* random;

    Program* cached_plugin;
    Host* cached_node;
    Process* cached_process;
    Event* cached_event;

    GHashTable* privatePrograms;

    MAGIC_DECLARE;
};

/* holds a thread-private key that each thread references to get a private
 * instance of a worker object */
static GPrivate workerKey = G_PRIVATE_INIT((GDestroyNotify)worker_free);

static Worker* _worker_getPrivate() {
    /* get current thread's private worker object */
    Worker* worker = g_private_get(&workerKey);
    MAGIC_ASSERT(worker);
    return worker;
}

gboolean worker_isAlive() {
    return g_private_get(&workerKey) != NULL;
}

Worker* worker_new(Slave* slave) {
    /* make sure this isnt called twice on the same thread! */
    utility_assert(!worker_isAlive());

    Worker* worker = g_new0(Worker, 1);
    MAGIC_INIT(worker);

    worker->slave = slave;
    worker->thread_id = slave_generateWorkerID(slave);
    worker->clock_now = SIMTIME_INVALID;
    worker->clock_last = SIMTIME_INVALID;
    worker->clock_barrier = SIMTIME_INVALID;

    /* each worker needs a private copy of each plug-in library */
    worker->privatePrograms = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)program_free);

    if(slave_getWorkerCount(slave) <= 1) {
        /* this will cause events to get pushed to this queue instead of host queues */
        worker->serialEventQueue = eventqueue_new();
    }

    g_private_replace(&workerKey, worker);

    return worker;
}

void worker_free(Worker* worker) {
    MAGIC_ASSERT(worker);

    /* calls the destroy functions we specified in g_hash_table_new_full */
    g_hash_table_destroy(worker->privatePrograms);

    if(worker->serialEventQueue) {
        eventqueue_free(worker->serialEventQueue);
    }

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

    debug("worker %i using plug-in at %p", worker->thread_id, privateProg);

    return privateProg;
}

static guint _worker_processNode(Worker* worker, Host* node, SimulationTime barrier) {
    /* update cache, reset clocks */
    worker->cached_node = node;
    worker->clock_last = SIMTIME_INVALID;
    worker->clock_now = SIMTIME_INVALID;
    worker->clock_barrier = barrier;

    /* lock the node */
    host_lock(worker->cached_node);

    EventQueue* eventq = host_getEvents(worker->cached_node);
    Event* nextEvent = eventqueue_peek(eventq);

    /* process all events in the nodes local queue */
    guint nEventsProcessed = 0;
    while(nextEvent && (shadowevent_getTime(nextEvent) < worker->clock_barrier))
    {
        worker->cached_event = eventqueue_pop(eventq);

        /* make sure we don't jump backward in time */
        worker->clock_now = shadowevent_getTime(worker->cached_event);
        if(worker->clock_last != SIMTIME_INVALID) {
            utility_assert(worker->clock_now >= worker->clock_last);
        }

        /* do the local task */
        gboolean complete = shadowevent_run(worker->cached_event);

        /* update times */
        worker->clock_last = worker->clock_now;
        worker->clock_now = SIMTIME_INVALID;

        /* finished event can now be destroyed */
        if(complete) {
            shadowevent_free(worker->cached_event);
            nEventsProcessed++;
        }

        /* get the next event, or NULL will tell us to break */
        nextEvent = eventqueue_peek(eventq);
    }

    /* unlock, clear cache */
    host_unlock(worker->cached_node);
    worker->cached_node = NULL;
    worker->cached_event = NULL;

    return nEventsProcessed;
}

gpointer worker_runParallel(WorkLoad* workload) {
    utility_assert(workload);
    /* get current thread's private worker object */
    Worker* worker = worker_new(workload->slave);

    /* continuously run all events for this worker's assigned nodes.
     * the simulation is done when the engine is killed. */
    while(!slave_isKilled(worker->slave)) {
        SimulationTime barrier = slave_getExecutionBarrier(worker->slave);
        guint nEventsProcessed = 0;
        guint nNodesWithEvents = 0;

        GList* item = workload->hosts;
        while(item) {
            Host* node = item->data;
            guint n = _worker_processNode(worker, node, barrier);
            nEventsProcessed += n;
            if(n > 0) {
                nNodesWithEvents++;
            }
            item = g_list_next(item);
        }

        slave_notifyProcessed(worker->slave, nEventsProcessed, nNodesWithEvents);
    }

    /* free all applications before freeing any of the nodes since freeing
     * applications may cause close() to get called on sockets which needs
     * other node information.
     */
    GList* hosts = workload->hosts;
    while(hosts) {
        worker->cached_node = hosts->data;
        host_freeAllApplications(worker->cached_node);
        worker->cached_node = NULL;
        hosts = hosts->next;
    }

    g_list_foreach(workload->hosts, (GFunc) host_free, NULL);

//    g_thread_exit(NULL);
    return NULL;
}

gpointer worker_runSerial(WorkLoad* workload) {
    utility_assert(workload);
    Worker* worker = _worker_getPrivate();

    Event* nextEvent = eventqueue_peek(worker->serialEventQueue);
    if(nextEvent) {
        worker->clock_now = SIMTIME_INVALID;
        worker->clock_last = 0;

        /* process all events in the priority queue */
        while(nextEvent &&
            (shadowevent_getTime(nextEvent) < slave_getExecuteWindowEnd(worker->slave)) &&
            (shadowevent_getTime(nextEvent) < slave_getEndTime(worker->slave)))
        {
            /* get next event */
            nextEvent = eventqueue_pop(worker->serialEventQueue);
            worker->cached_event = nextEvent;
            worker->cached_node = shadowevent_getNode(nextEvent);

            /* ensure priority */
            worker->clock_now = shadowevent_getTime(worker->cached_event);
//          engine->clock = worker->clock_now;
            utility_assert(worker->clock_now >= worker->clock_last);

            gboolean complete = shadowevent_run(worker->cached_event);
            if(complete) {
                shadowevent_free(worker->cached_event);
            }
            worker->cached_event = NULL;
            worker->cached_node = NULL;
            worker->clock_last = worker->clock_now;
            worker->clock_now = SIMTIME_INVALID;

            nextEvent = eventqueue_peek(worker->serialEventQueue);
        }
    }

    slave_setKilled(worker->slave, TRUE);

    /* in single thread mode, we must free the nodes */
    GList* hosts = workload->hosts;
    while(hosts) {
        worker->cached_node = hosts->data;
        host_freeAllApplications(worker->cached_node);
        worker->cached_node = NULL;
        hosts = hosts->next;
    }

    g_list_foreach(workload->hosts, (GFunc) host_free, NULL);

    return NULL;
}

void worker_scheduleEvent(Event* event, SimulationTime nano_delay, GQuark receiver_node_id) {
    /* TODO create accessors, or better yet refactor the work to event class */
    utility_assert(event);

    /* get our thread-private worker */
    Worker* worker = _worker_getPrivate();

    /* when the event will execute */
    shadowevent_setTime(event, worker->clock_now + nano_delay);

    /* parties involved. sender may be NULL, receiver may not! */
    Host* sender = worker->cached_node;

    /* we MAY NOT OWN the receiver, so do not write to it! */
    Host* receiver = receiver_node_id == 0 ? sender : _slave_getHost(worker->slave, receiver_node_id);
    utility_assert(receiver);

    /* the NodeEvent needs a pointer to the correct node */
    shadowevent_setNode(event, receiver);

    /* if we are not going to execute any more events, free it and return */
    if(slave_isKilled(worker->slave)) {
        shadowevent_free(event);
        return;
    }

    /* engine is not killed, assert accurate worker clock */
    utility_assert(worker->clock_now != SIMTIME_INVALID);

    /* figure out where to push the event */
    if(worker->serialEventQueue) {
        /* single-threaded, push to global serial queue */
        eventqueue_push(worker->serialEventQueue, event);
    } else {
        /* non-local events must be properly delayed so the event wont show up at another worker
         * before the next scheduling interval. this is only a problem if the sender and
         * receivers have been assigned to different workers. */
        if(!host_isEqual(receiver, sender)) {
            SimulationTime jump = slave_getMinTimeJump(worker->slave);
            SimulationTime minTime = worker->clock_now + jump;

            /* warn and adjust time if needed */
            SimulationTime eventTime = shadowevent_getTime(event);
            if(eventTime < minTime) {
                info("Inter-node event time %"G_GUINT64_FORMAT" changed to %"G_GUINT64_FORMAT" due to minimum delay %"G_GUINT64_FORMAT,
                        eventTime, minTime, jump);
                shadowevent_setTime(event, minTime);
            }
        }

        /* multi-threaded, push event to receiver node */
        EventQueue* eventq = host_getEvents(receiver);
        eventqueue_push(eventq, event);
    }
}

void worker_schedulePacket(Packet* packet) {
    /* get our thread-private worker */
    Worker* worker = _worker_getPrivate();
    if(slave_isKilled(worker->slave)) {
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
    return worker->cached_node;
}

Process* worker_getActiveProcess() {
    Worker* worker = _worker_getPrivate();
    return worker->cached_process;
}

void worker_setActiveProcess(Process* proc) {
    Worker* worker = _worker_getPrivate();
    if(worker->cached_process) {
        process_unref(worker->cached_process);
        worker->cached_process = NULL;
    }
    if(proc) {
        process_ref(proc);
        worker->cached_process = proc;
    }
}

SimulationTime worker_getCurrentTime() {
    Worker* worker = _worker_getPrivate();
    return worker->clock_now;
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

void worker_addHost(Host* host, guint hostID) {
    Worker* worker = _worker_getPrivate();
    slave_addHost(worker->slave, host, hostID);
}

gint worker_getThreadID() {
    Worker* worker = _worker_getPrivate();
    return worker->thread_id;
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
    slave_heartbeat(worker->slave, worker->clock_now);
}

void worker_setCurrentTime(SimulationTime time) {
    Worker* worker = _worker_getPrivate();
    worker->clock_now = time;
}

gboolean worker_isFiltered(GLogLevelFlags level) {
    Worker* worker = worker_isAlive() ? _worker_getPrivate() : NULL;

    if(worker) {
        /* check the local node log level first */
        gboolean isNodeLevelSet = FALSE;
        Host* currentHost = worker_getCurrentHost();
        if(worker->cached_node) {
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

void worker_incrementPluginError() {
    Worker* worker = _worker_getPrivate();
    slave_incrementPluginError(worker->slave);
}

const gchar* worker_getHostsRootPath() {
    Worker* worker = _worker_getPrivate();
    return slave_getHostsRootPath(worker->slave);
}
