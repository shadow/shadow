/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <bits/types/struct_rusage.h>
#include <bits/types/struct_timeval.h>
#include <errno.h>
#include <glib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/resource.h>

#include "main/core/logger/shadow_logger.h"
#include "main/core/master.h"
#include "main/core/scheduler/scheduler.h"
#include "main/core/scheduler/scheduler_policy.h"
#include "main/core/slave.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/support/options.h"
#include "main/core/worker.h"
#include "main/host/host.h"
#include "main/host/network_interface.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/topology.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

typedef struct {

  /* the program name. */
  gchar* name;

  /* the path to the executable */
  gchar* path;

  /* the start symbol for the program */
  gchar* startSymbol;
  
  MAGIC_DECLARE;
} _ProgramMeta;

struct _Slave {
    Master* master;

    /* the worker object associated with the main thread of execution */
//    Worker* mainWorker;

    /* simulation cli options */
    Options* options;
    SimulationTime bootstrapEndTime;

    /* slave random source, init from master random, used to init host randoms */
    Random* random;
    guint rawFrequencyKHz;

    /* global object counters, we collect counts from workers at end of sim */
    ObjectCounter* objectCounts;

    /* the parallel event/host/thread scheduler */
    Scheduler* scheduler;

    /* the meta data for each program */
    GHashTable* programMeta;

    GMutex lock;
    GMutex pluginInitLock;

    /* We will not enter plugin context when set. Used when destroying threads */
    gboolean forceShadowContext;

    /* the last time we logged heartbeat information */
    SimulationTime simClockLastHeartbeat;

    guint numPluginErrors;

    gchar* cwdPath;
    gchar* dataPath;
    gchar* hostsPath;

    gchar* preloadShimPath;
    gchar* environment;

    MAGIC_DECLARE;
};

static Slave* globalSlave = NULL;

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
    const gchar* policyStr = options_getEventSchedulerPolicy(slave->options);
    if (g_ascii_strcasecmp(policyStr, "host") == 0) {
        return SP_PARALLEL_HOST_SINGLE;
    } else if (g_ascii_strcasecmp(policyStr, "steal") == 0) {
        return SP_PARALLEL_HOST_STEAL;
    } else if (g_ascii_strcasecmp(policyStr, "thread") == 0) {
        return SP_PARALLEL_THREAD_SINGLE;
    } else if (g_ascii_strcasecmp(policyStr, "threadXthread") == 0) {
        return SP_PARALLEL_THREAD_PERTHREAD;
    } else if (g_ascii_strcasecmp(policyStr, "threadXhost") == 0) {
        return SP_PARALLEL_THREAD_PERHOST;
    } else {
        error("unknown event scheduler policy '%s'; valid values are 'thread', 'host', 'threadXthread', or 'threadXhost'", policyStr);
        return SP_SERIAL_GLOBAL;
    }
}

_ProgramMeta* _program_meta_new(const gchar* name, const gchar* path, const gchar* startSymbol) {
    if((name == NULL) || (path == NULL)) {
        error("attempting to register a program with a null name and/or path");
    }

    _ProgramMeta* meta = g_new0(_ProgramMeta, 1);
    MAGIC_INIT(meta);

    meta->name = g_strdup(name);
    meta->path = g_strdup(path);

    if(startSymbol != NULL) {
        meta->startSymbol = g_strdup(startSymbol);
    }

    return meta;
}

void _program_meta_free(gpointer data) {
    _ProgramMeta* meta = (_ProgramMeta*)data;
    MAGIC_ASSERT(meta);

    if(meta->name) {
        g_free(meta->name);
    }

    if(meta->path) {
        g_free(meta->path);
    }

    if(meta->startSymbol) {
        g_free(meta->startSymbol);
    }

    MAGIC_CLEAR(meta);
    g_free(meta);
}

static guint _slave_nextRandomUInt(Slave* slave) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    guint r = random_nextUInt(slave->random);
    _slave_unlock(slave);
    return r;
}

Slave* slave_new(Master* master, Options* options, SimulationTime endTime, SimulationTime unlimBWEndTime,
        guint randomSeed, const gchar* preloadShimPath, const gchar* environment) {
    if(globalSlave != NULL) {
        return NULL;
    }

    Slave* slave = g_new0(Slave, 1);
    MAGIC_INIT(slave);
    globalSlave = slave;

    g_mutex_init(&(slave->lock));
    g_mutex_init(&(slave->pluginInitLock));

    slave->master = master;
    slave->options = options;
    slave->random = random_new(randomSeed);
    slave->objectCounts = objectcounter_new();
    slave->bootstrapEndTime = unlimBWEndTime;
    slave->preloadShimPath = g_strdup(preloadShimPath);
    slave->environment = g_strdup(environment);

    slave->rawFrequencyKHz = utility_getRawCPUFrequency(CONFIG_CPU_MAX_FREQ_FILE);
    if(slave->rawFrequencyKHz == 0) {
        info("unable to read '%s' for copying", CONFIG_CPU_MAX_FREQ_FILE);
    }

    /* we will store the plug-in program meta data */
    slave->programMeta = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, _program_meta_free);

    /* the main scheduler may utilize multiple threads */

    guint nWorkers = options_getNWorkerThreads(options);
    SchedulerPolicyType policy = _slave_getEventSchedulerPolicy(slave);
    guint schedulerSeed = _slave_nextRandomUInt(slave);
    slave->scheduler = scheduler_new(policy, nWorkers, slave, schedulerSeed, endTime);

    slave->cwdPath = g_get_current_dir();
    slave->dataPath = g_build_filename(slave->cwdPath, options_getDataOutputPath(options), NULL);
    slave->hostsPath = g_build_filename(slave->dataPath, "hosts", NULL);

    if(g_file_test(slave->dataPath, G_FILE_TEST_EXISTS)) {
        gboolean success = utility_removeAll(slave->dataPath);
        utility_assert(success);
    }

    gchar* templateDataPath = g_build_filename(slave->cwdPath, options_getDataTemplatePath(options), NULL);
    if(g_file_test(templateDataPath, G_FILE_TEST_EXISTS)) {
        gboolean success = utility_copyAll(templateDataPath, slave->dataPath);
        utility_assert(success);
    }
    g_free(templateDataPath);

    /* now make sure the hosts path exists, as it may not have been in the template */
    g_mkdir_with_parents(slave->hostsPath, 0775);

    return slave;
}

gint slave_free(Slave* slave) {
    MAGIC_ASSERT(slave);
    gint returnCode = (slave->numPluginErrors > 0) ? -1 : 0;

    /* we will never execute inside the plugin again */
    slave->forceShadowContext = TRUE;

    if(slave->scheduler) {
        /* stop all of the threads and release host resources first */
        scheduler_shutdown(slave->scheduler);
        /* now we are the last one holding a ref, free the sched */
        scheduler_unref(slave->scheduler);
    }

    if(slave->objectCounts != NULL) {
        message("%s", objectcounter_valuesToString(slave->objectCounts));
        message("%s", objectcounter_diffsToString(slave->objectCounts));
        objectcounter_free(slave->objectCounts);
    }

    g_hash_table_destroy(slave->programMeta);

    g_mutex_clear(&(slave->lock));
    g_mutex_clear(&(slave->pluginInitLock));

    if (slave->cwdPath) {
        g_free(slave->cwdPath);
    }
    if (slave->dataPath) {
        g_free(slave->dataPath);
    }
    if (slave->hostsPath) {
        g_free(slave->hostsPath);
    }
    if(slave->random) {
        random_free(slave->random);
    }
    if(slave->preloadShimPath) {
        g_free(slave->preloadShimPath);
    }
    if(slave->environment) {
        g_free(slave->environment);
    }

    MAGIC_CLEAR(slave);
    g_free(slave);
    globalSlave = NULL;

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

void slave_addNewProgram(Slave* slave, const gchar* name, const gchar* path, const gchar* startSymbol) {
    MAGIC_ASSERT(slave);

    /* store the path to the plugin and maybe the start symbol with the given
     * name so that we can retrieve the path later when hosts' processes want
     * to load it */
    if(g_hash_table_lookup(slave->programMeta, name) != NULL) {
        error("attempting to regiser 2 plugins with the same path."
              "this should have been caught by the configuration parser.");
    } else {
        _ProgramMeta* meta = _program_meta_new(name, path, startSymbol);
        g_hash_table_replace(slave->programMeta, g_strdup(name), meta);
    }
}

void slave_addNewVirtualHost(Slave* slave, HostParameters* params) {
    MAGIC_ASSERT(slave);

    /* quarks are unique per slave process, so do the conversion here */
    params->id = g_quark_from_string(params->hostname);
    params->nodeSeed = _slave_nextRandomUInt(slave);

    Host* host = host_new(params);
    host_setup(host, slave_getDNS(slave), slave_getTopology(slave),
            slave_getRawCPUFrequency(slave), slave_getHostsRootPath(slave));
    scheduler_addHost(slave->scheduler, host);
}


static gchar** _slave_generateEnvv(Slave* slave, const gchar* preloadShimPath, const gchar* environment, const gchar* pluginPreloadPath) {
    MAGIC_ASSERT(slave);

    /* start with an empty environment */
    gchar** envv = g_environ_setenv(NULL, "SHADOW_SPAWNED", "TRUE", TRUE);

    /* insert also the plugin preload entry if one exists.
     * precendence here is:
     *   - preload attribute in the process element
     *   - preload attribute in the shadow element
     *   - preload values from LD_PRELOAD entries in the environment attribute of the shadow element*/
    GString* ldPreloadVal = g_string_new(NULL);
    if(pluginPreloadPath) {
        g_string_printf(ldPreloadVal, "%s:%s", pluginPreloadPath, preloadShimPath);
    } else {
        g_string_printf(ldPreloadVal, "%s", preloadShimPath);
    }

    /* now we also have to scan the other env variables that were given in the shadow conf file */

    if(environment) {
        /* entries are split by ';' */
        gchar** envTokens = g_strsplit(environment, ";", 0);

        for(gint i = 0; envTokens[i] != NULL; i++) {
            /* each env entry is key=value, get 2 tokens max */
            gchar** items = g_strsplit(envTokens[i], "=", 2);

            gchar* key = items[0];
            gchar* value = items[1];

            if(key != NULL && value != NULL) {
                /* check if the key is LD_PRELOAD */
                if(!g_ascii_strncasecmp(key, "LD_PRELOAD", 10)) {
                    /* append all LD_PRELOAD entries */
                    gchar** preloadTokens = g_strsplit(value, ":", 0);

                    for(gint j = 0; preloadTokens[j] != NULL; j++) {
                        g_string_append_printf(ldPreloadVal, ":%s", preloadTokens[j]);
                    }

                    g_strfreev(preloadTokens);
                } else {
                    /* set the key=value pair, but don't overwrite any existing settings */
                    envv = g_environ_setenv(envv, key, value, 0);
                }
            }

            g_strfreev(items);
        }

        g_strfreev(envTokens);
    }

    /* now we can set the LD_PRELOAD environment */
    envv = g_environ_setenv(envv, "LD_PRELOAD", ldPreloadVal->str, TRUE);

    /* cleanup */
    g_string_free(ldPreloadVal, TRUE);

    return envv;
}

static gchar** _slave_getArgv(Slave* slave, gchar* exepath, gchar* arguments) {
    MAGIC_ASSERT(slave);

    /* we need at least the executable path in order to run the plugin */
    GString* command = g_string_new(exepath);

    /* if the user specified additional arguments, append those */
    if(arguments && (g_ascii_strncasecmp(arguments, "\0", (gsize) 1) != 0)) {
        g_string_append_printf(command, " %s", arguments);
    }

    /* now split the command string to an argv */
    gchar** argv = g_strsplit(command->str, " ", 0);

    /* we don't need the command string anymore */
    g_string_free(command, TRUE);

    return argv;
}

void slave_addNewVirtualProcess(Slave* slave, gchar* hostName, gchar* pluginName, gchar* preloadName,
        SimulationTime startTime, SimulationTime stopTime, gchar* arguments) {
    MAGIC_ASSERT(slave);

    /* quarks are unique per process, so do the conversion here */
    GQuark hostID = g_quark_from_string(hostName);

    _ProgramMeta* plugin = g_hash_table_lookup(slave->programMeta, pluginName);
    if(plugin == NULL) {
        error("plugin not found for name '%s'. this should be verified in the "
              "config parser.", pluginName);
    }

    _ProgramMeta* preload = NULL;
    if(preloadName != NULL) {
        preload = g_hash_table_lookup(slave->programMeta, preloadName);
        if(preload == NULL) {
            error("preload plugin not found for name '%s'. this should be verified in the config parser", preloadName);
        }
    }

    /* ownership is passed to the host/process below, so we don't free these */
    gchar** envv = _slave_generateEnvv(slave, slave->preloadShimPath, slave->environment, preload ? preload->path : NULL);
    gchar** argv = _slave_getArgv(slave, plugin->path, arguments);

    Host* host = scheduler_getHost(slave->scheduler, hostID);
    host_continueExecutionTimer(host);
    host_addApplication(host, startTime, stopTime,
            plugin->name, plugin->path, plugin->startSymbol, envv, argv);
    host_stopExecutionTimer(host);
}

DNS* slave_getDNS(Slave* slave) {
    MAGIC_ASSERT(slave);
    return master_getDNS(slave->master);
}

Topology* slave_getTopology(Slave* slave) {
    MAGIC_ASSERT(slave);
    return master_getTopology(slave->master);
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
    return master_getLatency(slave->master, sourceAddress, destinationAddress);
}

Options* slave_getOptions(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->options;
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

static void _slave_heartbeat(Slave* slave, SimulationTime simClockNow) {
    MAGIC_ASSERT(slave);

    if(simClockNow > (slave->simClockLastHeartbeat + options_getHeartbeatInterval(slave->options))) {
        slave->simClockLastHeartbeat = simClockNow;

        struct rusage resources;
        if(!getrusage(RUSAGE_SELF, &resources)) {
            /* success, convert the values */
            gdouble maxMemory = ((gdouble)resources.ru_maxrss)/((gdouble)1048576.0f); // Kib->GiB
            gdouble userTimeMinutes = ((gdouble)resources.ru_utime.tv_sec)/((gdouble)60.0f);
            gdouble systemTimeMinutes = ((gdouble)resources.ru_stime.tv_sec)/((gdouble)60.0f);

            /* log the usage results */
            message("process resource usage at simtime %"G_GUINT64_FORMAT" reported by getrusage(): "
                    "ru_maxrss=%03f GiB, ru_utime=%03f minutes, ru_stime=%03f minutes, ru_nvcsw=%li, ru_nivcsw=%li",
                    simClockNow, maxMemory, userTimeMinutes, systemTimeMinutes, resources.ru_nvcsw, resources.ru_nivcsw);
        } else {
            warning("unable to print process resources usage: error %i in getrusage: %s", errno, g_strerror(errno));
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
        data->notifyDoneRunning = NULL; // we don't need to be notified in single thread mode

        /* the worker takes control of data pointer and frees it */
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

            /* do some idle processing here if needed */
            /* TODO the heartbeat should run in single process mode too! */
            _slave_heartbeat(slave, windowStart);

            /* flush slave threads messages */
            shadow_logger_flushRecords(shadow_logger_getDefault(),
                                       pthread_self());

            /* let the logger know it can flush everything prior to this round */
            shadow_logger_syncToDisk(shadow_logger_getDefault());

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

void slave_incrementPluginError(Slave* slave) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    slave->numPluginErrors++;
    _slave_unlock(slave);
}

const gchar* slave_getHostsRootPath(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->hostsPath;
}

void slave_storeCounts(Slave* slave, ObjectCounter* objectCounter) {
    MAGIC_ASSERT(slave);
    _slave_lock(slave);
    if(slave->objectCounts) {
        objectcounter_incrementAll(globalSlave->objectCounts, objectCounter);
    }
    _slave_unlock(slave);
}

void slave_countObject(ObjectType otype, CounterType ctype) {
    if(globalSlave) {
        MAGIC_ASSERT(globalSlave);
        _slave_lock(globalSlave);
        if(globalSlave->objectCounts) {
            objectcounter_incrementOne(globalSlave->objectCounts, otype, ctype);
        }
        _slave_unlock(globalSlave);
    }
}

SimulationTime slave_getBootstrapEndTime(Slave* slave) {
    MAGIC_ASSERT(slave);
    return slave->bootstrapEndTime;
}
