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
//    Worker* mainWorker;

    /* simulation configuration options */
    Configuration* config;

    /* slave random source, init from master random, used to init host randoms */
    Random* random;
    gint rawFrequencyKHz;

    /* network connectivity */
    Topology* topology;
    DNS* dns;

    /* the parallel event/host/thread scheduler */
    Scheduler* scheduler;

    /* the program code that is run by virtual processes */
    GHashTable* programs;

    GMutex lock;
    GMutex pluginInitLock;

    /* We will not enter plugin context when set. Used when destroying threads */
    gboolean forceShadowContext;

    /* the last time we logged heartbeat information */
    SimulationTime simClockLastHeartbeat;

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

static Host* _slave_getHost(Slave* slave, GQuark hostID) {
    MAGIC_ASSERT(slave);
    return scheduler_getHost(slave->scheduler, hostID);
}

/* XXX this really belongs in the configuration file */
static SchedulerPolicyType _slave_getEventSchedulerPolicy(Slave* slave) {
    const gchar* policy = configuration_getEventSchedulerPolicy(slave->config);
    if (g_ascii_strcasecmp(policy, "host") == 0) {
        return SP_PARALLEL_HOST_SINGLE;
    } else if (g_ascii_strcasecmp(policy, "thread") == 0) {
        return SP_PARALLEL_THREAD_SINGLE;
    } else if (g_ascii_strcasecmp(policy, "threadXthread") == 0) {
        return SP_PARALLEL_THREAD_PERTHREAD;
    } else if (g_ascii_strcasecmp(policy, "threadXhost") == 0) {
        return SP_PARALLEL_THREAD_PERHOST;
    } else {
        error("unknown event scheduler policy '%s'; valid values are 'thread', 'host', 'threadXthread', or 'threadXhost'", policy);
        return SP_SERIAL_GLOBAL;
    }
}

Slave* slave_new(Master* master, Configuration* config, guint randomSeed, GQueue* initActions) {
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

    /* we will store the plug-in programs that are loaded */
    slave->programs = g_hash_table_new_full(g_int_hash, g_int_equal, NULL, (GDestroyNotify)program_free);

    /* initialize DNS subsystem for this slave */
    slave->dns = dns_new();

    /* the main scheduler may utilize multiple threads */

    guint nWorkers = configuration_getNWorkerThreads(config);
    SchedulerPolicyType policy = _slave_getEventSchedulerPolicy(slave);
    guint schedulerSeed = (guint)slave_nextRandomInt(slave);
    slave->scheduler = scheduler_new(policy, nWorkers, slave, initActions, schedulerSeed);

    return slave;
}

void slave_free(Slave* slave) {
    MAGIC_ASSERT(slave);

    /* we will never execute inside the plugin again */
    slave->forceShadowContext = TRUE;

    if(slave->scheduler) {
        scheduler_unref(slave->scheduler);
    }
    if(slave->topology) {
        topology_free(slave->topology);
    }
    if(slave->dns) {
        dns_free(slave->dns);
    }

    g_hash_table_destroy(slave->programs);

    g_mutex_clear(&(slave->lock));
    g_mutex_clear(&(slave->pluginInitLock));

    /* free main worker */
//    worker_free(slave->mainWorker);

    MAGIC_CLEAR(slave);
    g_free(slave);
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

void slave_setKillTime(Slave* slave, SimulationTime endTime) {
    MAGIC_ASSERT(slave);
    master_setEndTime(slave->master, endTime);
    scheduler_setEndTime(slave->scheduler, endTime);
}

gboolean slave_schedulerIsRunning(Slave* slave) {
    MAGIC_ASSERT(slave);
    return scheduler_isRunning(slave->scheduler);
}

void slave_updateMinTimeJump(Slave* slave, gdouble minPathLatency) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    /* this update will get applied at the next round update, so all threads
     * running now still have a valid round window */
    master_updateMinTimeJump(slave->master, minPathLatency);
    _slave_unlock(slave);
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

void slave_run(Slave* slave) {
    MAGIC_ASSERT(slave);
    if(scheduler_getPolicy(slave->scheduler) == SP_SERIAL_GLOBAL) {
        scheduler_start(slave->scheduler);

        /* the main slave thread becomes the only worker and runs everything */
        WorkerRunData* data = g_new0(WorkerRunData, 1);
        data->threadID = 0;
        data->scheduler = slave->scheduler;
        data->userData = slave;
        worker_run(data);

        scheduler_finish(slave->scheduler);
    } else {
        /* we are the main thread, we manage the execution window updates while the workers run events */
        SimulationTime windowStart = 0, windowEnd = 1;
        SimulationTime minNextEventTime = SIMTIME_INVALID;
        gboolean keepRunning = TRUE;

        scheduler_start(slave->scheduler);

        while(keepRunning) {
            /* release the workers and run next round */
            scheduler_continueNextRound(slave->scheduler, windowStart, windowEnd);

            /* we could eventually do some idle processing here if needed */

            /* wait for the workers to finish processing nodes before we update the execution window */
            minNextEventTime = scheduler_awaitNextRound(slave->scheduler);

            /* we are in control now, the workers are waiting for the next round */
            info("finished execution window [%"G_GUINT64_FORMAT"--%"G_GUINT64_FORMAT"] next event at %"G_GUINT64_FORMAT,
                    windowStart, windowEnd, minNextEventTime);

            /* notify master that we finished this round, and the time of our next event
             * in order to fast-forward our execute window if possible */
            keepRunning = master_slaveFinishedCurrentRound(slave->master, minNextEventTime, &windowStart, &windowEnd);
        }

        scheduler_finish(slave->scheduler);
    }
}
