/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <glib.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "igraph_version.h"
#include "main/bindings/c/bindings.h"
#include "main/core/controller.h"
#include "main/core/logger/shadow_logger.h"
#include "main/core/support/configuration.h"
#include "main/core/support/options.h"
#include "main/host/affinity.h"
#include "main/shmem/shmem_cleanup.h"
#include "main/utility/disable_aslr.h"
#include "main/utility/utility.h"
#include "shd-config.h"
#include "support/logger/logger.h"

static bool _useSchedFifo = false;
OPTION_EXPERIMENTAL_ENTRY(
    "set-sched-fifo", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &_useSchedFifo,
    "Use the SCHED_FIFO scheduler. Requires CAP_SYS_NICE. See sched(7), capabilities(7)", NULL)

static Controller* shadowcontroller;

static void _main_logEnvironment(gchar** argv, gchar** envv) {
    /* log all args */
    if(argv) {
        for(gint i = 0; argv[i] != NULL; i++) {
            message("arg: %s", argv[i]);
        }
    }

    /* log some useful environment variables at message, the rest at debug */
    if(envv) {
        for(gint i = 0; envv[i] != NULL; i++) {
            if(!g_ascii_strncasecmp(envv[i], "LD_PRELOAD", 10) ||
                    !g_ascii_strncasecmp(envv[i], "SHADOW_SPAWNED", 14) ||
                    !g_ascii_strncasecmp(envv[i], "LD_STATIC_TLS_EXTRA", 19) ||
                    !g_ascii_strncasecmp(envv[i], "G_DEBUG", 7) ||
                    !g_ascii_strncasecmp(envv[i], "G_SLICE", 7)) {
                message("env: %s", envv[i]);
            } else {
                debug("env: %s", envv[i]);
            }
        }
    }
}

static gint _main_helper(Options* options, gchar* argv[]) {
    /* start off with some status messages */
#if defined(IGRAPH_VERSION)
    gint igraphMajor = -1, igraphMinor = -1, igraphPatch = -1;
    igraph_version(NULL, &igraphMajor, &igraphMinor, &igraphPatch);

    gchar* startupStr = g_strdup_printf("Starting %s with GLib v%u.%u.%u and IGraph v%i.%i.%i",
            SHADOW_VERSION_STRING,
            (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
            igraphMajor, igraphMinor, igraphPatch);
#else
    gchar* startupStr = g_strdup_printf("Starting %s with GLib v%u.%u.%u (IGraph version not available)",
            SHADOW_VERSION_STRING,
            (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
#endif

    message("%s", startupStr);
    /* avoid logging the message to stderr twice (only log if this is not a relaunch) */
    if(g_getenv("SHADOW_SPAWNED") == NULL) {
        g_printerr("** %s\n", startupStr);
    }
    g_free(startupStr);

    message(SHADOW_BUILD_STRING);
    message(SHADOW_INFO_STRING);
    message("logging current startup arguments and environment");

    gchar** envlist = g_get_environ();
    _main_logEnvironment(argv, envlist);
    g_strfreev(envlist);

    message("startup checks passed, we are ready to start simulation");

    /* pause for debugger attachment if the option is set */
    if(options_doRunDebug(options)) {
        gint pid = (gint)getpid();
        message("Pausing with SIGTSTP to enable debugger attachment (pid %i)", pid);
        g_printerr("** Pausing with SIGTSTP to enable debugger attachment (pid %i)\n", pid);
        raise(SIGTSTP);
        message("Resuming now");
    }

    /* allocate and initialize our main simulation driver */
    gint returnCode = 0;
    shadowcontroller = controller_new(options);

    if (shadowcontroller) {
        /* run the simulation */
        returnCode = controller_run(shadowcontroller);

        /* cleanup */
        controller_free(shadowcontroller);
        shadowcontroller = NULL;
    }

    message("%s simulation was shut down cleanly, returning code %i", SHADOW_VERSION_STRING, returnCode);
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
    Options* options = options_new(argc, argv);
    if(!options) {
        return EXIT_FAILURE;
    }

    // If we are just printing the version or running a cleanup+exit,
    // then print the version, cleanup if requested, and exit with success.
    if (options_doRunPrintVersion(options) ||
        options_shouldExitAfterShmCleanup(options)) {
        g_printerr("%s running GLib v%u.%u.%u and IGraph v%s\n%s\n%s\n",
                SHADOW_VERSION_STRING,
                (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
#if defined(IGRAPH_VERSION)
                IGRAPH_VERSION,
#else
                "(n/a)",
#endif
                SHADOW_BUILD_STRING,
                SHADOW_INFO_STRING);

        if (options_shouldExitAfterShmCleanup(options)) {
            shmemcleanup_tryCleanup();
        }

        options_free(options);
        return EXIT_SUCCESS;
    }

    /* start up the logging subsystem to handle all future messages */
    ShadowLogger* shadowLogger =
        shadow_logger_new(options_getLogLevel(options));
    shadow_logger_setDefault(shadowLogger);
    rust_logging_init();

    /* disable buffering during startup so that we see every message immediately in the terminal */
    shadow_logger_setEnableBuffering(shadowLogger, FALSE);

#ifndef DEBUG
    if (options_getLogLevel(options) == LOGLEVEL_DEBUG) {
        warning("Log level set to %s, but Shadow was not built in debug mode",
                loglevel_toStr(options_getLogLevel(options)));
    }
#endif

    // before we run the simluation, clean up any orphaned shared memory
    shmemcleanup_tryCleanup();
    
    if (options_getCPUPinning(options)) {
        int rc = affinity_initPlatformInfo();
        if (rc) {
            return EXIT_FAILURE;
        }
    }

    if (_useSchedFifo) {
        struct sched_param param = {0};
        param.sched_priority = 1;
        int rc = sched_setscheduler(0, SCHED_FIFO, &param);

        if (rc != 0) {
            error("Could not set SCHED_FIFO");
        } else {
            message("Successfully set real-time scheduler mode to SCHED_FIFO");
        }
    }

    // Disable address space layout randomization of processes forked from this
    // one to ensure determinism in cases when an executable under simulation
    // branch on memory addresses.
    disable_aslr();

    gint returnCode = _main_helper(options, argv);

    options_free(options);
    ShadowLogger* logger = shadow_logger_getDefault();
    if(logger) {
        shadow_logger_setDefault(NULL);
        shadow_logger_unref(logger);
    }

    g_printerr("** Stopping Shadow, returning code %i (%s)\n", returnCode,
               (returnCode == 0) ? "success" : "error");
    return returnCode;
}
