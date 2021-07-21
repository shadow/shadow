/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <elf.h>
#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <link.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/resource.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/controller.h"
#include "main/core/manager.h"
#include "main/core/scheduler/scheduler.h"
#include "main/core/scheduler/scheduler_policy.h"
#include "main/core/support/config_handlers.h"
#include "main/core/support/definitions.h"
#include "main/host/host.h"
#include "main/host/network_interface.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/routing/topology.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"

#define PRELOAD_SHIM_LIB_STR "libshadow-shim.so"
#define PRELOAD_OPENSSL_RNG_LIB_STR "libshadow_openssl_rng.so"

// Allow turning off openssl rng lib preloading at run-time.
static bool _use_openssl_rng_preload = true;
ADD_CONFIG_HANDLER(config_getUseOpensslRNGPreload, _use_openssl_rng_preload)

struct _Manager {
    Controller* controller;

    ChildPidWatcher* watcher;

    /* the worker object associated with the main thread of execution */
    //    Worker* mainWorker;

    /* simulation cli options */
    ConfigOptions* config;
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

    // Path to the shim that we preload for every managed process.
    gchar* preloadShimPath;
    // Path to the openssl rng lib that we preload for requesting managed processes.
    gchar* preloadOpensslRngPath;

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

static gchar* _manager_getRPath() {
    const ElfW(Dyn) *dyn = _DYNAMIC;
    const ElfW(Dyn) *rpath = NULL;
    const gchar *strtab = NULL;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        if (dyn->d_tag == DT_RPATH || dyn->d_tag == DT_RUNPATH) {
            rpath = dyn;
        } else if (dyn->d_tag == DT_STRTAB) {
            strtab = (const gchar *) dyn->d_un.d_val;
        }
    }
    GString* rpathStrBuf = g_string_new(NULL );
    if (strtab != NULL && rpath != NULL ) {
        g_string_printf(rpathStrBuf, "%s", strtab + rpath->d_un.d_val);
    }
    return g_string_free(rpathStrBuf, FALSE);
}

static gboolean _manager_isValidPathToPreloadLib(const gchar* path, const gchar* libname) {
    if(path) {
        gboolean isAbsolute = g_path_is_absolute(path);
        gboolean exists = g_file_test(path, G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS);
        gboolean hasLibName = g_str_has_suffix(path, libname);

        if (isAbsolute && exists && hasLibName) {
            return TRUE;
        }
    }

    return FALSE;
}

static gchar* _manager_scanRPathForLib(const gchar* libname) {
    gchar* preloadArgValue = NULL;

    gchar* rpathStr = _manager_getRPath();
    if(rpathStr != NULL) {
        gchar** tokens = g_strsplit(rpathStr, ":", 0);

        for(gint i = 0; tokens[i] != NULL; i++) {
            GString* candidateBuffer = g_string_new(NULL);

            /* rpath specifies directories, so look inside */
            g_string_printf(candidateBuffer, "%s/%s", tokens[i], libname);
            gchar* candidate = g_string_free(candidateBuffer, FALSE);

            if (_manager_isValidPathToPreloadLib(candidate, libname)) {
                preloadArgValue = candidate;
                break;
            } else {
                g_free(candidate);
            }
        }

        g_strfreev(tokens);
    }
    g_free(rpathStr);

    return preloadArgValue;
}

static guint _manager_nextRandomUInt(Manager* manager) {
    MAGIC_ASSERT(manager);
    _manager_lock(manager);
    guint r = random_nextUInt(manager->random);
    _manager_unlock(manager);
    return r;
}

ChildPidWatcher* manager_childpidwatcher(Manager* manager) { return manager->watcher; }

Manager* manager_new(Controller* controller, ConfigOptions* config, SimulationTime endTime,
                     SimulationTime unlimBWEndTime, guint randomSeed) {
    if (globalmanager != NULL) {
        return NULL;
    }

    Manager* manager = g_new0(Manager, 1);
    MAGIC_INIT(manager);
    globalmanager = manager;

    manager->watcher = childpidwatcher_new();

    g_mutex_init(&(manager->lock));
    g_mutex_init(&(manager->pluginInitLock));

    manager->controller = controller;
    manager->config = config;
    manager->random = random_new(randomSeed);
    manager->bootstrapEndTime = unlimBWEndTime;

    manager->rawFrequencyKHz = utility_getRawCPUFrequency(CONFIG_CPU_MAX_FREQ_FILE);
    if (manager->rawFrequencyKHz == 0) {
        debug("unable to read '%s' for copying", CONFIG_CPU_MAX_FREQ_FILE);
        manager->rawFrequencyKHz = 2500000; // 2.5 GHz
        trace("raw manager cpu frequency unavailable, using 2,500,000 KHz");
    }

    manager->preloadShimPath = _manager_scanRPathForLib(PRELOAD_SHIM_LIB_STR);
    if (manager->preloadShimPath != NULL) {
        info("found required preload library %s at path %s", PRELOAD_SHIM_LIB_STR,
             manager->preloadShimPath);
    } else {
        // The shim is required, so panic if we can't find it.
        utility_panic("could not find required preload library %s in rpath", PRELOAD_SHIM_LIB_STR);
    }

    manager->preloadOpensslRngPath = _manager_scanRPathForLib(PRELOAD_OPENSSL_RNG_LIB_STR);
    if (manager->preloadOpensslRngPath != NULL) {
        info("found optional preload library %s at path %s", PRELOAD_OPENSSL_RNG_LIB_STR,
             manager->preloadOpensslRngPath);
    } else {
        // The Openssl Rng is optional and may not be used by and managed processes.
        warning("could not find optional preload library %s in rpath", PRELOAD_OPENSSL_RNG_LIB_STR);
    }

    /* the main scheduler may utilize multiple threads */

    guint nWorkers = config_getWorkers(config);
    SchedulerPolicyType policy = config_getSchedulerPolicy(config);
    guint schedulerSeed = _manager_nextRandomUInt(manager);
    manager->scheduler =
        scheduler_new(manager, policy, nWorkers, schedulerSeed, endTime);

    manager->cwdPath = g_get_current_dir();

    char* dataDirectory = config_getDataDirectory(config);

    if (dataDirectory == NULL) {
        // we shouldn't reach this, but panic anyways
        utility_panic("Data directory was not set");
    }

    manager->dataPath = g_build_filename(manager->cwdPath, dataDirectory, NULL);
    config_freeString(dataDirectory);

    manager->hostsPath = g_build_filename(manager->dataPath, "hosts", NULL);

    if (g_file_test(manager->dataPath, G_FILE_TEST_EXISTS)) {
        utility_panic("data directory '%s' already exists", manager->dataPath);
    }

    char* templateDirectory = config_getTemplateDirectory(config);

    if (templateDirectory != NULL) {
        gchar* templateDataPath = g_build_filename(manager->cwdPath, templateDirectory, NULL);
        config_freeString(templateDirectory);

        debug("Copying template directory %s to %s", templateDataPath, manager->dataPath);

        if (!g_file_test(templateDataPath, G_FILE_TEST_EXISTS)) {
            utility_panic("data template directory '%s' does not exist", templateDataPath);
        }

        if (!utility_copyAll(templateDataPath, manager->dataPath)) {
            utility_panic("could not copy the data template directory '%s'", templateDataPath);
        }

        g_free(templateDataPath);
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

    if (manager->watcher) {
        childpidwatcher_free(manager->watcher);
        manager->watcher = NULL;
    }

    if (manager->scheduler) {
        /* stop all of the threads and release host resources first */
        scheduler_shutdown(manager->scheduler);
        /* now we are the last one holding a ref, free the sched */
        scheduler_unref(manager->scheduler);
    }

    if (manager->syscall_counter) {
        char* str = counter_alloc_string(manager->syscall_counter);
        info("Global syscall counts: %s", str);
        counter_free_string(manager->syscall_counter, str);
        counter_free(manager->syscall_counter);
    }

    if (manager->object_counter_alloc && manager->object_counter_dealloc) {
        char* str = counter_alloc_string(manager->object_counter_alloc);
        info("Global allocated object counts: %s", str);
        counter_free_string(manager->object_counter_alloc, str);

        str = counter_alloc_string(manager->object_counter_dealloc);
        info("Global deallocated object counts: %s", str);
        counter_free_string(manager->object_counter_dealloc, str);

        if (counter_equals_counter(
                manager->object_counter_alloc, manager->object_counter_dealloc)) {
            info("We allocated and deallocated the same number of objects :)");
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
    if (manager->preloadOpensslRngPath) {
        g_free(manager->preloadOpensslRngPath);
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
                                     const gchar* environment) {
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
    }

    /* insert also the plugin preload entry if one exists.
     * precendence here is:
     *   - preload path of the shim
     *   - preload path of the openssl rng lib
     *   - preload values from LD_PRELOAD entries in the environment process option */
    GPtrArray* ldPreloadArray = g_ptr_array_new();

    if (manager->preloadShimPath != NULL) {
        debug("adding Shadow preload shim path %s", manager->preloadShimPath);
        g_ptr_array_add(ldPreloadArray, g_strdup(manager->preloadShimPath));
    }

    if (!_use_openssl_rng_preload) {
        debug("openssl rng preloading is disabled");
    } else if (manager->preloadOpensslRngPath != NULL) {
        debug("adding Shadow preload lib path %s", manager->preloadOpensslRngPath);
        g_ptr_array_add(ldPreloadArray, g_strdup(manager->preloadOpensslRngPath));
    }

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
                    // Handle the list of entries, which could be separated by ' ' or ':'.
                    GString* preloadBuf = g_string_new(value);

                    // First replace all occurences of ' ' by ':'.
                    // g_string_replace does what we want, but isn't available until glib 2.68.
                    for (int i = 0; i < preloadBuf->len; i++) {
                        if (preloadBuf->str[i] == ' ') {
                            preloadBuf->str[i] = ':';
                        }
                    }

                    // Now split by ':' to handle each preload path.
                    gchar** paths = g_strsplit(preloadBuf->str, ":", 0);

                    // Expand any paths that begin with '~'.
                    for (int i = 0; paths != NULL && paths[i] != NULL; i++) {
                        GString* pathBuf = g_string_new(NULL);

                        if (g_str_has_prefix(paths[i], "~/")) {
                            g_string_printf(pathBuf, "%s%s", g_get_home_dir(), &paths[i][1]);
                        } else if (g_str_has_prefix(paths[i], "~")) {
                            g_string_printf(pathBuf, "/home/%s", &paths[i][1]);
                        } else {
                            g_string_printf(pathBuf, "%s", paths[i]);
                        }

                        debug("adding Process preload lib path %s", pathBuf->str);
                        g_ptr_array_add(ldPreloadArray, pathBuf->str);
                        g_string_free(pathBuf, FALSE);
                    }

                    // Cleanup.
                    g_strfreev(paths);
                    g_string_free(preloadBuf, TRUE);
                } else {
                    /* set the key=value pair, but don't overwrite any existing settings */
                    envv = g_environ_setenv(envv, key, value, 0);
                }
            }

            g_strfreev(items);
        }

        g_strfreev(envTokens);
    }

    /* must be NULL terminated for g_strjoinv */
    g_ptr_array_add(ldPreloadArray, NULL);

    gchar* ldPreloadVal = g_strjoinv(":", (gchar**)ldPreloadArray->pdata);
    g_ptr_array_unref(ldPreloadArray);

    /* now we can set the LD_PRELOAD environment */
    debug("Setting process env LD_PRELOAD=%s", ldPreloadVal);
    envv = g_environ_setenv(envv, "LD_PRELOAD", ldPreloadVal, TRUE);

    /* cleanup */
    g_free(ldPreloadVal);

    return envv;
}

void manager_addNewVirtualProcess(Manager* manager, const gchar* hostName, gchar* pluginPath,
                                  SimulationTime startTime, SimulationTime stopTime, gchar** argv,
                                  char* environment) {
    MAGIC_ASSERT(manager);

    /* quarks are unique per process, so do the conversion here */
    GQuark hostID = g_quark_from_string(hostName);

    InterposeMethod interposeMethod = config_getInterposeMethod(manager->config);

    /* ownership is passed to the host/process below, so we don't free these */
    gchar** envv = _manager_generateEnvv(manager, interposeMethod, environment);

    Host* host = scheduler_getHost(manager->scheduler, hostID);
    host_continueExecutionTimer(host);

    gchar* pluginName = g_path_get_basename(pluginPath);
    if (pluginName == NULL) {
        utility_panic("Could not get basename of plugin path");
    }

    host_addApplication(
        host, startTime, stopTime, interposeMethod, pluginName, pluginPath, envv, argv);
    g_free(pluginName);

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

const ConfigOptions* manager_getConfig(Manager* manager) {
    MAGIC_ASSERT(manager);
    return manager->config;
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

    SimulationTime heartbeatInterval = config_getHeartbeatInterval(manager->config);

    if (simClockNow > (manager->simClockLastHeartbeat + heartbeatInterval)) {
        manager->simClockLastHeartbeat = simClockNow;

        struct rusage resources;
        if (!getrusage(RUSAGE_SELF, &resources)) {
            /* success, convert the values */
            gdouble maxMemory = ((gdouble)resources.ru_maxrss) / ((gdouble)1048576.0f); // Kib->GiB
            gdouble userTimeMinutes = ((gdouble)resources.ru_utime.tv_sec) / ((gdouble)60.0f);
            gdouble systemTimeMinutes = ((gdouble)resources.ru_stime.tv_sec) / ((gdouble)60.0f);

            /* log the usage results */
            info("process resource usage at simtime %" G_GUINT64_FORMAT " reported by getrusage(): "
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

        /* wait for the workers to finish processing nodes before we update the
         * execution window
         */
        minNextEventTime = scheduler_awaitNextRound(manager->scheduler);

        /* we are in control now, the workers are waiting for the next round */
        debug("finished execution window [%" G_GUINT64_FORMAT "--%" G_GUINT64_FORMAT
              "] next event at %" G_GUINT64_FORMAT,
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
