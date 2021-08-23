/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <errno.h>
#include <glib.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/controller.h"
#include "main/core/logger/log_wrapper.h"
#include "main/core/support/config_handlers.h"
#include "main/host/affinity.h"
#include "main/shmem/shmem_cleanup.h"
#include "main/utility/disable_aslr.h"
#include "main/utility/utility.h"
#include "shd-config.h"

static bool _useSchedFifo = false;
ADD_CONFIG_HANDLER(config_getUseSchedFifo, _useSchedFifo)

static Controller* shadowcontroller;

static void _main_logEnvironment(gchar** argv, gchar** envv) {
    /* log all args */
    if(argv) {
        for(gint i = 0; argv[i] != NULL; i++) {
            info("arg: %s", argv[i]);
        }
    }

    /* log some useful environment variables at info, the rest at trace */
    if(envv) {
        for(gint i = 0; envv[i] != NULL; i++) {
            if(!g_ascii_strncasecmp(envv[i], "LD_PRELOAD", 10) ||
                    !g_ascii_strncasecmp(envv[i], "SHADOW_SPAWNED", 14) ||
                    !g_ascii_strncasecmp(envv[i], "LD_STATIC_TLS_EXTRA", 19) ||
                    !g_ascii_strncasecmp(envv[i], "G_DEBUG", 7) ||
                    !g_ascii_strncasecmp(envv[i], "G_SLICE", 7)) {
                info("env: %s", envv[i]);
            } else {
                trace("env: %s", envv[i]);
            }
        }
    }
}

static int _raise_rlimit(int resource) {
    char* debug_name = "?";

    /* can add more as needed */
    switch (resource) {
        case RLIMIT_NOFILE: debug_name = "RLIMIT_NOFILE"; break;
        case RLIMIT_NPROC: debug_name = "RLIMIT_NPROC"; break;
    }

    struct rlimit lim = {0};
    if (getrlimit(resource, &lim) != 0) {
        error(
            "Could not get rlimit for resource %s (%d): %s", debug_name, resource, strerror(errno));
        return -1;
    }

    lim.rlim_cur = lim.rlim_max;
    if (setrlimit(resource, &lim) != 0) {
        error("Could not update rlimit for resource %s (%d): %s", debug_name, resource,
              strerror(errno));
        return -1;
    }

    return 0;
}

static void _check_mitigations() {
    int state = prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_STORE_BYPASS, 0, 0, 0);
    if (state == -1) {
        panic("prctl: %s", g_strerror(errno));
    }
    if (state & PR_SPEC_DISABLE) {
        warning("Speculative Store Bypass sidechannel mitigation is enabled (perhaps by seccomp?). "
                "This typically adds ~30%% performance overhead.");
    }
}

static gint _main_helper(CliOptions* options, ConfigOptions* config, gchar* argv[]) {
    /* start off with some status messages */
    gchar* startupStr = g_strdup_printf("Starting %s with GLib v%u.%u.%u", SHADOW_VERSION_STRING,
                                        (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION,
                                        (guint)GLIB_MICRO_VERSION);

    info("%s", startupStr);
    /* avoid logging the message to stderr twice (only log if this is not a relaunch) */
    if(g_getenv("SHADOW_SPAWNED") == NULL) {
        g_printerr("** %s\n", startupStr);
    }
    g_free(startupStr);

    info(SHADOW_BUILD_STRING);
    info(SHADOW_INFO_STRING);
    info("logging current startup arguments and environment");

    gchar** envlist = g_get_environ();
    _main_logEnvironment(argv, envlist);
    g_strfreev(envlist);

    info("startup checks passed, we are ready to start simulation");

    /* pause for debugger attachment if the option is set */
    if(clioptions_getGdb(options)) {
        gint pid = (gint)getpid();
        info("Pausing with SIGTSTP to enable debugger attachment (pid %i)", pid);
        g_printerr("** Pausing with SIGTSTP to enable debugger attachment (pid %i)\n", pid);
        raise(SIGTSTP);
        info("Resuming now");
    }

    /* allocate and initialize our main simulation driver */
    gint returnCode = 0;
    shadowcontroller = controller_new(config);

    if (shadowcontroller) {
        /* run the simulation */
        returnCode = controller_run(shadowcontroller);

        /* cleanup */
        controller_free(shadowcontroller);
        shadowcontroller = NULL;
    }

    info("%s simulation was shut down cleanly, returning code %i", SHADOW_VERSION_STRING,
         returnCode);
    return returnCode;
}

gint main_runShadow(gint argc, gchar* argv[]) {

    /* check the compiled GLib version */
    if (!GLIB_CHECK_VERSION(2, 32, 0)) {
        g_printerr("** GLib version 2.32.0 or above is required but Shadow was compiled against version %u.%u.%u\n",
            (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return EXIT_FAILURE;
    }

    if(GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION == 40) {
        g_printerr("** You compiled against GLib version %u.%u.%u, which has bugs known to break Shadow. Please update to a newer version of GLib.\n",
                    (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return EXIT_FAILURE;
    }

    /* check the that run-time GLib matches the compiled version */
    const gchar* mismatch = glib_check_version(GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
    if(mismatch) {
        g_printerr("** The version of the run-time GLib library (%u.%u.%u) is not compatible with the version against which Shadow was compiled (%u.%u.%u). GLib message: '%s'\n",
        glib_major_version, glib_minor_version, glib_micro_version,
        (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
        mismatch);
        return EXIT_FAILURE;
    }

    /* unblock all signals in shadow and child processes since cmake's ctest blocks SIGTERM (and
     * maybe others) */
    sigset_t new_sig_set;
    sigemptyset(&new_sig_set);
    sigprocmask(SIG_SETMASK, &new_sig_set, NULL);

    /* parse the options from the command line */
    CliOptions* options = clioptions_parse(argc, (const char**)argv);
    if (!options) {
        return EXIT_FAILURE;
    }

    if (clioptions_getShowBuildInfo(options)) {
        g_printerr("%s running GLib v%u.%u.%u and IGraph v%s\n%s\n%s\n", SHADOW_VERSION_STRING,
                   (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
#if defined(IGRAPH_VERSION)
                   IGRAPH_VERSION,
#else
                   "(n/a)",
#endif
                   SHADOW_BUILD_STRING, SHADOW_INFO_STRING);

        clioptions_free(options);
        return EXIT_SUCCESS;
    }

    if (clioptions_getShmCleanup(options)) {
        shmemcleanup_tryCleanup();

        clioptions_free(options);
        return EXIT_SUCCESS;
    }

    char* configName = clioptions_getConfig(options);

    /* since we've already checked the two exclusive flags above (--show-build-info and
     * --shm-cleanup), configName should never be NULL*/
    if (!configName) {
        utility_panic("Could not get configuration file path");
    }

    /* read config from file or stdin */
    ConfigFileOptions* configFile = NULL;
    if (strcmp(configName, "-") != 0) {
        configFile = configfile_parse(configName);
    } else {
        configFile = configfile_parse("/dev/stdin");
    }

    clioptions_freeString(configName);

    /* configFile may be NULL if the config file doesn't exist or could not be parsed correctly */
    if (!configFile) {
        clioptions_free(options);
        return EXIT_FAILURE;
    }

    /* generate the final shadow configuration from the config file and cli options */
    ConfigOptions* config = config_new(configFile, options);
    configfile_free(configFile);

    if (clioptions_getShowConfig(options)) {
        config_showConfig(config);

        clioptions_free(options);
        config_free(config);
        return EXIT_SUCCESS;
    }

    runConfigHandlers(config);

    LogLevel logLevel = config_getLogLevel(config);

    /* start up the logging subsystem to handle all future messages */
    shadow_logger_init();
    logger_setDefault(rustlogger_new(logLevel));
    logger_setLevel(logger_getDefault(), logLevel);

    /* disable buffering during startup so that we see every message immediately in the terminal */
    shadow_logger_setEnableBuffering(FALSE);

#ifndef DEBUG
    if (logLevel == LOGLEVEL_TRACE) {
        warning("Log level set to %s, but Shadow was not built in debug mode",
                loglevel_toStr(logLevel));
    }
#endif

    // before we run the simluation, clean up any orphaned shared memory
    shmemcleanup_tryCleanup();

    if (config_getUseCpuPinning(config)) {
        int rc = affinity_initPlatformInfo();
        if (rc) {
            clioptions_free(options);
            config_free(config);
            return EXIT_FAILURE;
        }
    }

    /* raise fd soft limit to hard limit */
    if (_raise_rlimit(RLIMIT_NOFILE)) {
        return EXIT_FAILURE;
    }

    /* raise number of processes/threads soft limit to hard limit */
    if (_raise_rlimit(RLIMIT_NPROC)) {
        return EXIT_FAILURE;
    }

    if (_useSchedFifo) {
        struct sched_param param = {0};
        param.sched_priority = 1;
        int rc = sched_setscheduler(0, SCHED_FIFO, &param);

        if (rc != 0) {
            clioptions_free(options);
            config_free(config);
            utility_panic("Could not set SCHED_FIFO");
        } else {
            info("Successfully set real-time scheduler mode to SCHED_FIFO");
        }
    }

    // Disable address space layout randomization of processes forked from this
    // one to ensure determinism in cases when an executable under simulation
    // branch on memory addresses.
    disable_aslr();

    // Check sidechannel mitigations
    _check_mitigations();

    gint returnCode = _main_helper(options, config, argv);

    clioptions_free(options);
    config_free(config);

    // Flush the logger
    logger_flush(logger_getDefault());

    g_printerr("** Stopping Shadow, returning code %i (%s)\n", returnCode,
               (returnCode == 0) ? "success" : "error");
    return returnCode;
}
