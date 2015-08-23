/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "shadow.h"

#include <sys/time.h>
#include <sys/resource.h>

struct _Slave {
    Master* master;

    /* the worker object associated with the main thread of execution */
    Worker* mainThreadWorker;

    /* simulation configuration options */
    Configuration* config;

    /* slave random source, init from master random, used to init host randoms */
    Random* random;

    /* network connectivity */
    Topology* topology;
    DNS* dns;

    /* virtual hosts */
    GHashTable* hosts;

    GHashTable* programs;

    /* if multi-threaded, we use worker thread */
    CountDownLatch* processingLatch;
    CountDownLatch* barrierLatch;

    /* the number of worker threads not counting main thread.
     * this is the number of threads we need to spawn. */
    guint nWorkers;

    /* id generation counters, must be protected for thread safety */
    volatile gint workerIDCounter;

    GMutex lock;
    GMutex pluginInitLock;

    gint rawFrequencyKHz;
    guint numEventsCurrentInterval;
    guint numNodesWithEventsCurrentInterval;

    /* We will not enter plugin context when set. Used when destroying threads */
    gboolean forceShadowContext;

    /* the last time we logged heartbeat information */
    SimulationTime simClockLastHeartbeat;

    guint numPluginErrors;

    gchar* cwdPath;
    gchar* dataPath;
    gchar* hostsPath;

    MAGIC_DECLARE;
};

static void _slave_lock(Slave* slave) {
    MAGIC_ASSERT(slave);
    g_mutex_lock(&(slave->lock));
}

static void _slave_unlock(Slave* slave) {
    MAGIC_ASSERT(slave);
    g_mutex_unlock(&(slave->lock));
}

// TODO make this static
Host* _slave_getHost(Slave* slave, GQuark hostID) {
    MAGIC_ASSERT(slave);
    return (Host*) g_hash_table_lookup(slave->hosts, GUINT_TO_POINTER((guint)hostID));
}

void slave_addHost(Slave* slave, Host* host, guint hostID) {
    MAGIC_ASSERT(slave);
    g_hash_table_replace(slave->hosts, GUINT_TO_POINTER(hostID), host);
}

static GList* _slave_getAllHosts(Slave* slave) {
    MAGIC_ASSERT(slave);
    return g_hash_table_get_values(slave->hosts);
}

Slave* slave_new(Master* master, Configuration* config, guint randomSeed) {
    Slave* slave = g_new0(Slave, 1);
    MAGIC_INIT(slave);

    g_mutex_init(&(slave->lock));
    g_mutex_init(&(slave->pluginInitLock));

    slave->master = master;
    slave->config = config;
    slave->random = random_new(randomSeed);

    slave->rawFrequencyKHz = utility_getRawCPUFrequency(CONFIG_CPU_MAX_FREQ_FILE);
    if(slave->rawFrequencyKHz == 0) {
        info("unable to read '%s' for copying", CONFIG_CPU_MAX_FREQ_FILE);
    }

    slave->hosts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    slave->programs = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)program_free);

    slave->dns = dns_new();

    slave->nWorkers = (guint) configuration_getNWorkerThreads(config);
    slave->mainThreadWorker = worker_new(slave);

    slave->cwdPath = g_get_current_dir();
    slave->dataPath = g_build_filename(slave->cwdPath, "shadow.data", NULL);
    slave->hostsPath = g_build_filename(slave->dataPath, "hosts", NULL);

    if(g_file_test(slave->dataPath, G_FILE_TEST_EXISTS)) {
        gboolean success = utility_removeAll(slave->dataPath);
        utility_assert(success);
    }

    gchar* templateDataPath = g_build_filename(slave->cwdPath, "shadow.data.template", NULL);
    if(g_file_test(templateDataPath, G_FILE_TEST_EXISTS)) {
        gboolean success = utility_copyAll(templateDataPath, slave->dataPath);
        utility_assert(success);
    }
    g_free(templateDataPath);

    return slave;
}

gint slave_free(Slave* slave) {
    MAGIC_ASSERT(slave);
    gint returnCode = (slave->numPluginErrors > 0) ? -1 : 0;

    /* this launches delete on all the plugins and should be called before
     * the engine is marked "killed" and workers are destroyed.
     */
    g_hash_table_destroy(slave->hosts);

    /* we will never execute inside the plugin again */
    slave->forceShadowContext = TRUE;

    if(slave->topology) {
        topology_free(slave->topology);
    }
    if(slave->dns) {
        dns_free(slave->dns);
    }

    g_hash_table_destroy(slave->programs);

    g_mutex_clear(&(slave->lock));
    g_mutex_clear(&(slave->pluginInitLock));

    /* join and free spawned worker threads */
//TODO

    if(slave->cwdPath) {
        g_free(slave->cwdPath);
    }
    if(slave->dataPath) {
        g_free(slave->dataPath);
    }
    if(slave->hostsPath) {
        g_free(slave->hostsPath);
    }

    /* free main worker */
    worker_free(slave->mainThreadWorker);

    MAGIC_CLEAR(slave);
    g_free(slave);

    return returnCode;
}

gboolean slave_isForced(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->forceShadowContext;
}

guint slave_getRawCPUFrequency(Slave* slave) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    guint freq = slave->rawFrequencyKHz;
    _slave_unlock(slave);
    return freq;
}

gint slave_nextRandomInt(Slave* slave) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    gint r = random_nextInt(slave->random);
    _slave_unlock(slave);
    return r;
}

gdouble slave_nextRandomDouble(Slave* slave) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    gdouble r = random_nextDouble(slave->random);
    _slave_unlock(slave);
    return r;
}

GTimer* slave_getRunTimer(Slave* slave) {
    return master_getRunTimer(slave->master);
}

gint slave_generateWorkerID(Slave* slave) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    gint id = slave->workerIDCounter;
    (slave->workerIDCounter)++;
    _slave_unlock(slave);
    return id;
}

void slave_storeProgram(Slave* slave, Program* prog) {
    MAGIC_ASSERT(slave);
    g_hash_table_insert(slave->programs, program_getID(prog), prog);
}

Program* slave_getProgram(Slave* slave, GQuark pluginID) {
    MAGIC_ASSERT(slave);
    return g_hash_table_lookup(slave->programs, &pluginID);
}

DNS* slave_getDNS(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->dns;
}

Topology* slave_getTopology(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->topology;
}

void slave_setTopology(Slave* slave, Topology* topology) {
    MAGIC_ASSERT(slave);
    slave->topology = topology;
}

guint32 slave_getNodeBandwidthUp(Slave* slave, GQuark nodeID, in_addr_t ip) {
    MAGIC_ASSERT(slave);
    Host* host = _slave_getHost(slave, nodeID);
    NetworkInterface* interface = host_lookupInterface(host, ip);
    return networkinterface_getSpeedUpKiBps(interface);
}

guint32 slave_getNodeBandwidthDown(Slave* slave, GQuark nodeID, in_addr_t ip) {
    MAGIC_ASSERT(slave);
    Host* host = _slave_getHost(slave, nodeID);
    NetworkInterface* interface = host_lookupInterface(host, ip);
    return networkinterface_getSpeedDownKiBps(interface);
}

gdouble slave_getLatency(Slave* slave, GQuark sourceNodeID, GQuark destinationNodeID) {
    MAGIC_ASSERT(slave);
    Host* sourceNode = _slave_getHost(slave, sourceNodeID);
    Host* destinationNode = _slave_getHost(slave, destinationNodeID);
    Address* sourceAddress = host_getDefaultAddress(sourceNode);
    Address* destinationAddress = host_getDefaultAddress(destinationNode);
    return topology_getLatency(slave->topology, sourceAddress, destinationAddress);
}

Configuration* slave_getConfig(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->config;
}

SimulationTime slave_getExecuteWindowEnd(Slave* slave) {
    MAGIC_ASSERT(slave);
    return master_getExecuteWindowEnd(slave->master);
}

SimulationTime slave_getEndTime(Slave* slave) {
    MAGIC_ASSERT(slave);
    return master_getEndTime(slave->master);
}

gboolean slave_isKilled(Slave* slave) {
    MAGIC_ASSERT(slave);
    return master_isKilled(slave->master);
}

void slave_setKillTime(Slave* slave, SimulationTime endTime) {
    MAGIC_ASSERT(slave);
    master_setKillTime(slave->master, endTime);
}

void slave_setKilled(Slave* slave, gboolean isKilled) {
    MAGIC_ASSERT(slave);
    master_setKilled(slave->master, isKilled);
}

SimulationTime slave_getMinTimeJump(Slave* slave) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    SimulationTime jump = master_getMinTimeJump(slave->master);
    _slave_unlock(slave);
    return jump;
}

void slave_updateMinTimeJump(Slave* slave, gdouble minPathLatency) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    master_updateMinTimeJump(slave->master, minPathLatency);
    _slave_unlock(slave);
}

guint slave_getWorkerCount(Slave* slave) {
    MAGIC_ASSERT(slave);
    /* configured number of worker threads, + 1 for main thread */
    return slave->nWorkers + 1;
}

SimulationTime slave_getExecutionBarrier(Slave* slave) {
    MAGIC_ASSERT(slave);
    return master_getExecutionBarrier(slave->master);
}

void slave_heartbeat(Slave* slave, SimulationTime simClockNow) {
    MAGIC_ASSERT(slave);

    gboolean shouldLogResourceUsage = FALSE;

    /* do as little as possible while holding the lock */
    _slave_lock(slave);
    /* XXX: this should be done asynchronously */
    if(simClockNow > slave->simClockLastHeartbeat) {
        shouldLogResourceUsage = TRUE;
        slave->simClockLastHeartbeat = simClockNow;
    }
    _slave_unlock(slave);

    if(shouldLogResourceUsage) {
        struct rusage resources;
        if(!getrusage(RUSAGE_SELF, &resources)) {
            /* success, convert the values */
            gdouble maxMemory = ((gdouble)resources.ru_maxrss)/((gdouble)1048576.0f); // Kib->GiB
            gdouble userTimeMinutes = ((gdouble)resources.ru_utime.tv_sec)/((gdouble)60.0f);
            gdouble systemTimeMinutes = ((gdouble)resources.ru_stime.tv_sec)/((gdouble)60.0f);

            /* log the usage results */
            message("process resource usage reported by getrusage(): "
                    "ru_maxrss=%03f GiB, ru_utime=%03f minutes, ru_stime=%03f minutes, ru_nvcsw=%li, ru_nivcsw=%li",
                    maxMemory, userTimeMinutes, systemTimeMinutes, resources.ru_nvcsw, resources.ru_nivcsw);
        } else {
            warning("unable to print process resources usage: error in getrusage: %i", errno);
        }
    }
}

void slave_notifyProcessed(Slave* slave, guint numberEventsProcessed, guint numberNodesWithEvents) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    slave->numEventsCurrentInterval += numberEventsProcessed;
    slave->numNodesWithEventsCurrentInterval += numberNodesWithEvents;
    _slave_unlock(slave);
    countdownlatch_countDownAwait(slave->processingLatch);
    countdownlatch_countDownAwait(slave->barrierLatch);
}

void slave_runParallel(Slave* slave) {
    MAGIC_ASSERT(slave);

    GList* nodeList = _slave_getAllHosts(slave);

    /* assign nodes to the worker threads so they get processed */
    WorkLoad workArray[slave->nWorkers];
    memset(workArray, 0, slave->nWorkers * sizeof(WorkLoad));
    gint counter = 0;

    GList* item = g_list_first(nodeList);
    while(item) {
        Host* node = item->data;

        gint i = counter % slave->nWorkers;
        workArray[i].hosts = g_list_append(workArray[i].hosts, node);

        counter++;
        item = g_list_next(item);
    }

    /* we will track when workers finish processing their nodes */
    slave->processingLatch = countdownlatch_new(slave->nWorkers + 1);
    /* after the workers finish processing, wait for barrier update */
    slave->barrierLatch = countdownlatch_new(slave->nWorkers + 1);

    /* start up the workers */
    GSList* workerThreads = NULL;
    for(gint i = 0; i < slave->nWorkers; i++) {
        GString* name = g_string_new(NULL);
        g_string_printf(name, "worker-%i", (i+1));

        workArray[i].slave = slave;
        workArray[i].master = slave->master;

        GThread* t = g_thread_new(name->str, (GThreadFunc)worker_runParallel, &(workArray[i]));
        workerThreads = g_slist_append(workerThreads, t);

        g_string_free(name, TRUE);
    }

    message("started %i worker threads", slave->nWorkers);

    /* process all events in the priority queue */
    while(master_getExecuteWindowStart(slave->master) < master_getEndTime(slave->master))
    {
        /* wait for the workers to finish processing nodes before we touch them */
        countdownlatch_countDownAwait(slave->processingLatch);

        /* we are in control now, the workers are waiting at barrierLatch */
        info("execution window [%"G_GUINT64_FORMAT"--%"G_GUINT64_FORMAT"] ran %u events from %u active nodes",
                master_getExecuteWindowStart(slave->master), master_getExecuteWindowEnd(slave->master),
                slave->numEventsCurrentInterval, slave->numNodesWithEventsCurrentInterval);

        /* check if we should take 1 step ahead or fast-forward our execute window.
         * since looping through all the nodes to find the minimum event is
         * potentially expensive, we use a heuristic of only trying to jump ahead
         * if the last interval had only a few events in it. */
        SimulationTime minNextEventTime = SIMTIME_INVALID;
        if(slave->numEventsCurrentInterval < 10) {
            /* we had no events in that interval, lets try to fast forward */
            item = g_list_first(nodeList);

            /* fast forward to the next event, the new window start will be the next event time */
            while(item) {
                Host* node = item->data;
                EventQueue* eventq = host_getEvents(node);
                Event* nextEvent = eventqueue_peek(eventq);
                SimulationTime nextEventTime = shadowevent_getTime(nextEvent);
                if(nextEvent && (nextEventTime < minNextEventTime)) {
                    minNextEventTime = nextEventTime;
                }
                item = g_list_next(item);
            }

            if(minNextEventTime == SIMTIME_INVALID) {
                minNextEventTime = master_getExecuteWindowEnd(slave->master);
            }
        } else {
            /* we still have events, lets just step one interval,
             * consider the next event as the previous window end */
            minNextEventTime = master_getExecuteWindowEnd(slave->master);
        }

        /* notify master that we finished this round, and what our next event is */
        master_slaveFinishedCurrentWindow(slave->master, minNextEventTime);

        /* reset for next round */
        countdownlatch_reset(slave->processingLatch);
        slave->numEventsCurrentInterval = 0;
        slave->numNodesWithEventsCurrentInterval = 0;

        /* release the workers for the next round, or to exit */
        countdownlatch_countDownAwait(slave->barrierLatch);
        countdownlatch_reset(slave->barrierLatch);
    }

    message("waiting for %i worker threads to finish", slave->nWorkers);

    /* wait for the threads to finish their cleanup */
    GSList* threadItem = workerThreads;
    while(threadItem) {
        GThread* t = threadItem->data;
        /* the join will consume the reference, so unref is not needed */
        g_thread_join(t);
        threadItem = g_slist_next(threadItem);
    }
    g_slist_free(workerThreads);

    message("%i worker threads finished", slave->nWorkers);

    for(gint i = 0; i < slave->nWorkers; i++) {
        WorkLoad w = workArray[i];
        g_list_free(w.hosts);
    }

    countdownlatch_free(slave->processingLatch);
    countdownlatch_free(slave->barrierLatch);

    /* frees the list struct we own, but not the nodes it holds (those were
     * taken care of by the workers) */
    g_list_free(nodeList);
}

void slave_runSerial(Slave* slave) {
    MAGIC_ASSERT(slave);
    WorkLoad w;
    w.master = slave->master;
    w.slave = slave;
    w.hosts = _slave_getAllHosts(slave);
    worker_runSerial(&w);
    g_list_free(w.hosts);
}

void slave_incrementPluginError(Slave* slave) {
    MAGIC_ASSERT(slave);
    slave->numPluginErrors++;
}

const gchar* slave_getHostsRootPath(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->hostsPath;
}
