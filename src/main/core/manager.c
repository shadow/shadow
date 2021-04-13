/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/resource.h>

#include "main/bindings/c/bindings.h"
#include "main/core/controller.h"
#include "main/core/logger/shadow_logger.h"
#include "main/core/manager.h"
#include "main/core/scheduler/scheduler.h"
#include "main/core/scheduler/scheduler_policy.h"
#include "main/core/support/definitions.h"
#include "main/core/support/options.h"
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

struct _Manager {
    Controller* controller;

    /* the worker object associated with the main thread of execution */
    //    Worker* mainWorker;

    /* simulation cli options */
    Options* options;
    SimulationTime bootstrapEndTime;

    /* manager random source, init from controller random, used to init host randoms */
    Random* random;
    guint rawFrequencyKHz;

    /* global object counters, we collect counts from workers at end of sim */
    Counter* object_counter_alloc;
    Counter* object_counter_dealloc;

    // Global syscall counter, we collect counts from workers at end of sim
    Counter* syscall_counter;

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

static Manager* globalmanager = NULL;

static void _manager_lock(Manager* manager) {
    MAGIC_ASSERT(manager);
    g_mutex_lock(&(manager->lock));
}

static void _manager_unlock(Manager* manager) {
    MAGIC_ASSERT(manager);
    g_mutex_unlock(&(manager->lock));
}

static Host* _manager_getHost(Manager* manager, GQuark hostID) {
    MAGIC_ASSERT(manager);
    return scheduler_getHost(manager->scheduler, hostID);
}

/* XXX this really belongs in the configuration file */
static SchedulerPolicyType _manager_getEventSchedulerPolicy(Manager* manager) {
    const gchar* policyStr = options_getEventSchedulerPolicy(manager->options);
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
        error("unknown event scheduler policy '%s'; valid values are 'thread', 'host', "
              "'threadXthread', or 'threadXhost'",
              policyStr);
        return SP_SERIAL_GLOBAL;
    }
}

_ProgramMeta* _program_meta_new(const gchar* name, const gchar* path, const gchar* startSymbol) {
    if ((name == NULL) || (path == NULL)) {
        error("attempting to register a program with a null name and/or path");
    }

    _ProgramMeta* meta = g_new0(_ProgramMeta, 1);
    MAGIC_INIT(meta);

    meta->name = g_strdup(name);
    meta->path = g_strdup(path);

    if (startSymbol != NULL) {
        meta->startSymbol = g_strdup(startSymbol);
    }

    return meta;
}

void _program_meta_free(gpointer data) {
    _ProgramMeta* meta = (_ProgramMeta*)data;
    MAGIC_ASSERT(meta);

    if (meta->name) {
        g_free(meta->name);
    }

    if (meta->path) {
        g_free(meta->path);
    }

    if (meta->startSymbol) {
        g_free(meta->startSymbol);
    }

    MAGIC_CLEAR(meta);
    g_free(meta);
}

static guint _manager_nextRandomUInt(Manager* manager) {
    MAGIC_ASSERT(manager);
    _manager_lock(manager);
    guint r = random_nextUInt(manager->random);
    _manager_unlock(manager);
    return r;
}

Manager* manager_new(Controller* controller, Options* options, SimulationTime endTime,
                     SimulationTime unlimBWEndTime, guint randomSeed, const gchar* preloadShimPath,
                     const gchar* environment) {
    if (globalmanager != NULL) {
        return NULL;
    }

    Manager* manager = g_new0(Manager, 1);
    MAGIC_INIT(manager);
    globalmanager = manager;

    g_mutex_init(&(manager->lock));
    g_mutex_init(&(manager->pluginInitLock));

    manager->controller = controller;
    manager->options = options;
    manager->random = random_new(randomSeed);
    manager->bootstrapEndTime = unlimBWEndTime;
    manager->preloadShimPath = g_strdup(preloadShimPath);
    manager->environment = g_strdup(environment);

    manager->rawFrequencyKHz = utility_getRawCPUFrequency(CONFIG_CPU_MAX_FREQ_FILE);
    if (manager->rawFrequencyKHz == 0) {
        info("unable to read '%s' for copying", CONFIG_CPU_MAX_FREQ_FILE);
    }

    /* we will store the plug-in program meta data */
    manager->programMeta =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, _program_meta_free);

    /* the main scheduler may utilize multiple threads */

    guint nWorkers = options_getNWorkerThreads(options);
    SchedulerPolicyType policy = _manager_getEventSchedulerPolicy(manager);
    guint schedulerSeed = _manager_nextRandomUInt(manager);
    manager->scheduler =
        scheduler_new(manager, policy, nWorkers, schedulerSeed, endTime);

    manager->cwdPath = g_get_current_dir();
    manager->dataPath =
        g_build_filename(manager->cwdPath, options_getDataOutputPath(options), NULL);
    manager->hostsPath = g_build_filename(manager->dataPath, "hosts", NULL);

    if (g_file_test(manager->dataPath, G_FILE_TEST_EXISTS)) {
        error("data directory '%s' already exists", manager->dataPath);
    }

    const gchar* templateDataPath = options_getDataTemplatePath(options);
    if (templateDataPath != NULL) {
        gchar* absTemplatePath = g_build_filename(manager->cwdPath, templateDataPath, NULL);

        if (!g_file_test(absTemplatePath, G_FILE_TEST_EXISTS)) {
            error("data template directory '%s' does not exist", absTemplatePath);
        }

        if (!utility_copyAll(absTemplatePath, manager->dataPath)) {
            error("could not copy the data template directory '%s'", absTemplatePath);
        }

        g_free(absTemplatePath);
    } else {
        /* provide a warning for backwards compatibility; can remove this sometime in the future */
        gchar* compatTemplatePath =
            g_build_filename(manager->cwdPath, "shadow.data.template", NULL);
        if (g_file_test(compatTemplatePath, G_FILE_TEST_EXISTS)) {
            warning("The directory 'shadow.data.template' exists, but '--data-template' was not "
                    "set. Ignore this warning if this was intentional.");
        }
        g_free(compatTemplatePath);
    }

    /* now make sure the hosts path exists, as it may not have been in the template */
    g_mkdir_with_parents(manager->hostsPath, 0775);

    return manager;
}

gint manager_free(Manager* manager) {
    MAGIC_ASSERT(manager);
    gint returnCode = (manager->numPluginErrors > 0) ? -1 : 0;

    /* we will never execute inside the plugin again */
    manager->forceShadowContext = TRUE;

    if (manager->scheduler) {
        /* stop all of the threads and release host resources first */
        scheduler_shutdown(manager->scheduler);
        /* now we are the last one holding a ref, free the sched */
        scheduler_unref(manager->scheduler);
    }

    if (manager->syscall_counter) {
        char* str = counter_alloc_string(manager->syscall_counter);
        message("Global syscall counts: %s", str);
        counter_free_string(manager->syscall_counter, str);
        counter_free(manager->syscall_counter);
    }

    if (manager->object_counter_alloc && manager->object_counter_dealloc) {
        char* str = counter_alloc_string(manager->object_counter_alloc);
        message("Global allocated object counts: %s", str);
        counter_free_string(manager->object_counter_alloc, str);

        str = counter_alloc_string(manager->object_counter_dealloc);
        message("Global deallocated object counts: %s", str);
        counter_free_string(manager->object_counter_dealloc, str);

        if (counter_equals_counter(
                manager->object_counter_alloc, manager->object_counter_dealloc)) {
            message("We allocated and deallocated the same number of objects :)");
        } else {
            /* don't change the formatting of this line as we search for it in test cases */
            warning("Memory leak detected");
        }
    }
    if (manager->object_counter_alloc) {
        counter_free(manager->object_counter_alloc);
    }
    if (manager->object_counter_dealloc) {
        counter_free(manager->object_counter_dealloc);
    }

    g_hash_table_destroy(manager->programMeta);

    g_mutex_clear(&(manager->lock));
    g_mutex_clear(&(manager->pluginInitLock));

    if (manager->cwdPath) {
        g_free(manager->cwdPath);
    }
    if (manager->dataPath) {
        g_free(manager->dataPath);
    }
    if (manager->hostsPath) {
        g_free(manager->hostsPath);
    }
    if (manager->random) {
        random_free(manager->random);
    }
    if (manager->preloadShimPath) {
        g_free(manager->preloadShimPath);
    }
    if (manager->environment) {
        g_free(manager->environment);
    }

    MAGIC_CLEAR(manager);
    g_free(manager);
    globalmanager = NULL;

    return returnCode;
}

gboolean manager_isForced(Manager* manager) {
    MAGIC_ASSERT(manager);
    return manager->forceShadowContext;
}

guint manager_getRawCPUFrequency(Manager* manager) {
    MAGIC_ASSERT(manager);
    _manager_lock(manager);
    guint freq = manager->rawFrequencyKHz;
    _manager_unlock(manager);
    return freq;
}

void manager_addNewProgram(Manager* manager, const gchar* name, const gchar* path,
                           const gchar* startSymbol) {
    MAGIC_ASSERT(manager);

    /* store the path to the plugin and maybe the start symbol with the given
     * name so that we can retrieve the path later when hosts' processes want
     * to load it */
    if (g_hash_table_lookup(manager->programMeta, name) != NULL) {
        error("attempting to regiser 2 plugins with the same path."
              "this should have been caught by the configuration parser.");
    } else {
        _ProgramMeta* meta = _program_meta_new(name, path, startSymbol);
        g_hash_table_replace(manager->programMeta, g_strdup(name), meta);
    }
}

void manager_addNewVirtualHost(Manager* manager, HostParameters* params) {
    MAGIC_ASSERT(manager);

    /* quarks are unique per manager process, so do the conversion here */
    params->id = g_quark_from_string(params->hostname);
    params->nodeSeed = _manager_nextRandomUInt(manager);

    Host* host = host_new(params);
    host_setup(host, manager_getDNS(manager), manager_getTopology(manager),
               manager_getRawCPUFrequency(manager), manager_getHostsRootPath(manager));
    scheduler_addHost(manager->scheduler, host);
}

static gchar** _manager_generateEnvv(Manager* manager, InterposeMethod interposeMethod,
                                     const gchar* preloadShimPath, const gchar* environment,
                                     const gchar* pluginPreloadPath) {
    MAGIC_ASSERT(manager);

    /* start with an empty environment */
    gchar** envv = g_environ_setenv(NULL, "SHADOW_SPAWNED", "TRUE", TRUE);

    {
        // Pass the (real) start time to the plugin, so that shim-side logging
        // can log real time from the correct offset.
        char* timestring = g_strdup_printf("%" PRId64, logger_get_global_start_time_micros());
        envv = g_environ_setenv(envv, "SHADOW_LOG_START_TIME", timestring, TRUE);
        g_free(timestring);
    }

    switch (interposeMethod) {
        case INTERPOSE_METHOD_PTRACE:
            envv = g_environ_setenv(envv, "SHADOW_INTERPOSE_METHOD", "PTRACE", 0);
            break;
        case INTERPOSE_METHOD_PRELOAD:
            envv = g_environ_setenv(envv, "SHADOW_INTERPOSE_METHOD", "PRELOAD", 0);
            break;
        case INTERPOSE_METHOD_HYBRID:
            envv = g_environ_setenv(envv, "SHADOW_INTERPOSE_METHOD", "HYBRID", 0);
            break;
    }

    /* insert also the plugin preload entry if one exists.
     * precendence here is:
     *   - preload attribute in the process element
     *   - preload attribute in the shadow element
     *   - preload values from LD_PRELOAD entries in the environment attribute of the shadow
     * element*/
    GPtrArray* ldPreloadArray = g_ptr_array_new();
    if (pluginPreloadPath) {
        info("adding preload path %s", pluginPreloadPath);
        g_ptr_array_add(ldPreloadArray, g_strdup(pluginPreloadPath));
    }
    info("adding shim path %s", preloadShimPath ? preloadShimPath : "null");
    g_ptr_array_add(ldPreloadArray, g_strdup(preloadShimPath));

    /* now we also have to scan the other env variables that were given in the shadow conf file */

    if (environment) {
        /* entries are split by ';' */
        gchar** envTokens = g_strsplit(environment, ";", 0);

        for (gint i = 0; envTokens[i] != NULL; i++) {
            /* each env entry is key=value, get 2 tokens max */
            gchar** items = g_strsplit(envTokens[i], "=", 2);

            gchar* key = items[0];
            gchar* value = items[1];

            if (key != NULL && value != NULL) {
                /* check if the key is LD_PRELOAD */
                if (!g_ascii_strncasecmp(key, "LD_PRELOAD", 10)) {
                    /* append all LD_PRELOAD entries */
                    info("adding key path %s", value);
                    g_ptr_array_add(ldPreloadArray, g_strdup(value));
                } else {
                    /* set the key=value pair, but don't overwrite any existing settings */
                    envv = g_environ_setenv(envv, key, value, 0);
                }
            }

            g_strfreev(items);
        }

        g_strfreev(envTokens);
    }

    // Must be NULL terminated for g_strjoinv
    g_ptr_array_add(ldPreloadArray, NULL);

    gchar* ldPreloadVal = g_strjoinv(":", (gchar**)ldPreloadArray->pdata);
    g_ptr_array_unref(ldPreloadArray);

    /* now we can set the LD_PRELOAD environment */
    envv = g_environ_setenv(envv, "LD_PRELOAD", ldPreloadVal, TRUE);

    /* cleanup */
    g_free(ldPreloadVal);

    return envv;
}

static gchar** _manager_getArgv(Manager* manager, gchar* exepath, gchar* arguments) {
    MAGIC_ASSERT(manager);

    /* we need at least the executable path in order to run the plugin */
    GString* command = g_string_new(exepath);

    /* if the user specified additional arguments, append those */
    if (arguments && (g_ascii_strncasecmp(arguments, "\0", (gsize)1) != 0)) {
        g_string_append_printf(command, " %s", arguments);
    }

    /* now split the command string to an argv */
    gchar** argv = g_strsplit(command->str, " ", 0);

    /* we don't need the command string anymore */
    g_string_free(command, TRUE);

    return argv;
}

void manager_addNewVirtualProcess(Manager* manager, gchar* hostName, gchar* pluginName,
                                  gchar* preloadName, SimulationTime startTime,
                                  SimulationTime stopTime, gchar* arguments) {
    MAGIC_ASSERT(manager);

    /* quarks are unique per process, so do the conversion here */
    GQuark hostID = g_quark_from_string(hostName);

    _ProgramMeta* plugin = g_hash_table_lookup(manager->programMeta, pluginName);
    if (plugin == NULL) {
        error("plugin not found for name '%s'. this should be verified in the "
              "config parser.",
              pluginName);
    }

    InterposeMethod interposeMethod = options_getInterposeMethod(manager->options);

    _ProgramMeta* preload = NULL;
    if (preloadName != NULL) {
        preload = g_hash_table_lookup(manager->programMeta, preloadName);
        if (preload == NULL) {
            error("preload plugin not found for name '%s'. this should be verified in the config "
                  "parser",
                  preloadName);
        }
    }

    /* ownership is passed to the host/process below, so we don't free these */
    gchar** envv = _manager_generateEnvv(manager, interposeMethod, manager->preloadShimPath,
                                         manager->environment, preload ? preload->path : NULL);
    gchar** argv = _manager_getArgv(manager, plugin->path, arguments);

    Host* host = scheduler_getHost(manager->scheduler, hostID);
    host_continueExecutionTimer(host);
    host_addApplication(host, startTime, stopTime, interposeMethod, plugin->name, plugin->path,
                        plugin->startSymbol, envv, argv);
    host_stopExecutionTimer(host);
}

DNS* manager_getDNS(Manager* manager) {
    MAGIC_ASSERT(manager);
    return controller_getDNS(manager->controller);
}

Topology* manager_getTopology(Manager* manager) {
    MAGIC_ASSERT(manager);
    return controller_getTopology(manager->controller);
}

guint32 manager_getNodeBandwidthUp(Manager* manager, GQuark nodeID, in_addr_t ip) {
    MAGIC_ASSERT(manager);
    Host* host = _manager_getHost(manager, nodeID);
    NetworkInterface* interface = host_lookupInterface(host, ip);
    return networkinterface_getSpeedUpKiBps(interface);
}

guint32 manager_getNodeBandwidthDown(Manager* manager, GQuark nodeID, in_addr_t ip) {
    MAGIC_ASSERT(manager);
    Host* host = _manager_getHost(manager, nodeID);
    NetworkInterface* interface = host_lookupInterface(host, ip);
    return networkinterface_getSpeedDownKiBps(interface);
}

gdouble manager_getLatency(Manager* manager, GQuark sourceNodeID, GQuark destinationNodeID) {
    MAGIC_ASSERT(manager);
    Host* sourceNode = _manager_getHost(manager, sourceNodeID);
    Host* destinationNode = _manager_getHost(manager, destinationNodeID);
    Address* sourceAddress = host_getDefaultAddress(sourceNode);
    Address* destinationAddress = host_getDefaultAddress(destinationNode);
    return controller_getLatency(manager->controller, sourceAddress, destinationAddress);
}

Options* manager_getOptions(Manager* manager) {
    MAGIC_ASSERT(manager);
    return manager->options;
}

gboolean manager_schedulerIsRunning(Manager* manager) {
    MAGIC_ASSERT(manager);
    return scheduler_isRunning(manager->scheduler);
}

void manager_updateMinTimeJump(Manager* manager, gdouble minPathLatency) {
    MAGIC_ASSERT(manager);
    _manager_lock(manager);
    /* this update will get applied at the next round update, so all threads
     * running now still have a valid round window */
    controller_updateMinTimeJump(manager->controller, minPathLatency);
    _manager_unlock(manager);
}

static void _manager_heartbeat(Manager* manager, SimulationTime simClockNow) {
    MAGIC_ASSERT(manager);

    if (simClockNow >
        (manager->simClockLastHeartbeat + options_getHeartbeatInterval(manager->options))) {
        manager->simClockLastHeartbeat = simClockNow;

        struct rusage resources;
        if (!getrusage(RUSAGE_SELF, &resources)) {
            /* success, convert the values */
            gdouble maxMemory = ((gdouble)resources.ru_maxrss) / ((gdouble)1048576.0f); // Kib->GiB
            gdouble userTimeMinutes = ((gdouble)resources.ru_utime.tv_sec) / ((gdouble)60.0f);
            gdouble systemTimeMinutes = ((gdouble)resources.ru_stime.tv_sec) / ((gdouble)60.0f);

            /* log the usage results */
            message("process resource usage at simtime %" G_GUINT64_FORMAT
                    " reported by getrusage(): "
                    "ru_maxrss=%03f GiB, ru_utime=%03f minutes, ru_stime=%03f minutes, "
                    "ru_nvcsw=%li, ru_nivcsw=%li",
                    simClockNow, maxMemory, userTimeMinutes, systemTimeMinutes, resources.ru_nvcsw,
                    resources.ru_nivcsw);
        } else {
            warning("unable to print process resources usage: error %i in getrusage: %s", errno,
                    g_strerror(errno));
        }
    }
}

void manager_run(Manager* manager) {
    MAGIC_ASSERT(manager);
    /* we are the main thread, we manage the execution window updates while the
     * workers run events */
    SimulationTime windowStart = 0, windowEnd = 1;
    SimulationTime minNextEventTime = SIMTIME_INVALID;
    gboolean keepRunning = TRUE;

    scheduler_start(manager->scheduler);

    while (keepRunning) {
        /* release the workers and run next round */
        scheduler_continueNextRound(manager->scheduler, windowStart, windowEnd);

        /* do some idle processing here if needed */
        _manager_heartbeat(manager, windowStart);

        /* flush manager threads messages */
        shadow_logger_flushRecords(shadow_logger_getDefault(), pthread_self());

        /* let the logger know it can flush everything prior to this round */
        shadow_logger_syncToDisk(shadow_logger_getDefault());

        /* wait for the workers to finish processing nodes before we update the
         * execution window
         */
        minNextEventTime = scheduler_awaitNextRound(manager->scheduler);

        /* we are in control now, the workers are waiting for the next round */
        info("finished execution window [%" G_GUINT64_FORMAT
             "--%" G_GUINT64_FORMAT "] next event at %" G_GUINT64_FORMAT,
             windowStart, windowEnd, minNextEventTime);

        /* notify controller that we finished this round, and the time of our
         * next event in order to fast-forward our execute window if possible */
        keepRunning = controller_managerFinishedCurrentRound(
            manager->controller, minNextEventTime, &windowStart, &windowEnd);
    }

    scheduler_finish(manager->scheduler);
}

void manager_incrementPluginError(Manager* manager) {
    MAGIC_ASSERT(manager);
    _manager_lock(manager);
    manager->numPluginErrors++;
    _manager_unlock(manager);
}

const gchar* manager_getHostsRootPath(Manager* manager) {
    MAGIC_ASSERT(manager);
    return manager->hostsPath;
}

static void _manager_increment_object_counts(Manager* manager, Counter** mgr_obj_counts,
                                             const char* obj_name) {
    _manager_lock(manager);
    // This is created on the fly, so that if we did not enable counting mode
    // then we don't need to create the counter object.
    if (!*mgr_obj_counts) {
        *mgr_obj_counts = counter_new();
    }
    counter_add_value(*mgr_obj_counts, obj_name, 1);
    _manager_unlock(manager);
}

void manager_increment_object_alloc_counter_global(const char* object_name) {
    if (globalmanager) {
        MAGIC_ASSERT(globalmanager);
        _manager_increment_object_counts(
            globalmanager, &globalmanager->object_counter_alloc, object_name);
    }
}

void manager_increment_object_dealloc_counter_global(const char* object_name) {
    if (globalmanager) {
        MAGIC_ASSERT(globalmanager);
        _manager_increment_object_counts(
            globalmanager, &globalmanager->object_counter_dealloc, object_name);
    }
}

static void _manager_add_object_counts(Manager* manager, Counter** mgr_obj_counts,
                                       Counter* obj_counts) {
    _manager_lock(manager);
    // This is created on the fly, so that if we did not enable counting mode
    // then we don't need to create the counter object.
    if (!*mgr_obj_counts) {
        *mgr_obj_counts = counter_new();
    }
    counter_add_counter(*mgr_obj_counts, obj_counts);
    _manager_unlock(manager);
}

void manager_add_alloc_object_counts(Manager* manager, Counter* alloc_obj_counts) {
    MAGIC_ASSERT(manager);
    _manager_add_object_counts(manager, &manager->object_counter_alloc, alloc_obj_counts);
}

void manager_add_dealloc_object_counts(Manager* manager, Counter* dealloc_obj_counts) {
    MAGIC_ASSERT(manager);
    _manager_add_object_counts(manager, &manager->object_counter_dealloc, dealloc_obj_counts);
}

void manager_add_syscall_counts(Manager* manager, Counter* syscall_counts) {
    MAGIC_ASSERT(manager);
    _manager_lock(manager);
    // This is created on the fly, so that if we did not enable counting mode
    // then we don't need to create the counter object.
    if (!manager->syscall_counter) {
        manager->syscall_counter = counter_new();
    }
    counter_add_counter(manager->syscall_counter, syscall_counts);
    _manager_unlock(manager);
}

void manager_add_syscall_counts_global(Counter* syscall_counts) {
    if (globalmanager) {
        manager_add_syscall_counts(globalmanager, syscall_counts);
    }
}

SimulationTime manager_getBootstrapEndTime(Manager* manager) {
    MAGIC_ASSERT(manager);
    return manager->bootstrapEndTime;
}
