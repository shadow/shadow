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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "main/core/logger/shadow_logger.h"
#include "main/core/master.h"
#include "main/core/support/configuration.h"
#include "main/core/support/options.h"
#include "main/utility/utility.h"
#include "main/shmem/shmem_cleanup.h"
#include "igraph_version.h"
#include "shd-config.h"
#include "support/logger/logger.h"

static Master* shadowMaster;

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

static gint _main_helper(Options* options) {
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

    message(SHADOW_INFO_STRING);
    message("logging current startup arguments and environment");

    gchar** envlist = g_get_environ();
    gchar** arglist = g_strsplit(options_getArgumentString(options), " ", 0);
    _main_logEnvironment(arglist, envlist);
    g_strfreev(arglist);
    g_strfreev(envlist);

    /* check if we need to setup a valgrind environment and relaunch */
    if(g_getenv("SHADOW_SPAWNED") == NULL && options_doRunValgrind(options)) {
        message("shadow will automatically adjust valgrind environment and relaunch");

        /* now start to set up the environment */
        gchar** envlist = g_get_environ();
        GString* commandBuffer = g_string_new(options_getArgumentString(options));

        if(!envlist || !commandBuffer) {
            critical("there was a problem loading existing environment");
            return EXIT_FAILURE;
        }

        message("setting up environment for valgrind");

        /* make glib friendlier to valgrind */
        envlist = g_environ_setenv(envlist, "G_DEBUG", "gc-friendly", 0);
        envlist = g_environ_setenv(envlist, "G_SLICE", "always-malloc", 0);

        /* add the valgrind command and some default options */
        g_string_prepend(commandBuffer,
                        "valgrind --leak-check=full --show-reachable=yes --track-origins=yes --trace-children=yes --log-file=shadow-valgrind-%p.log --error-limit=no ");

        /* keep track that we are relaunching shadow */
        envlist = g_environ_setenv(envlist, "SHADOW_SPAWNED", "TRUE", 1);

        gchar* command = g_string_free(commandBuffer, FALSE);
        gchar** arglist = g_strsplit(command, " ", 0);
        g_free(command);

        message("environment was updated; shadow is relaunching now with new environment");

        ShadowLogger* logger = shadow_logger_getDefault();
        if(logger) {
            shadow_logger_setDefault(NULL);
            shadow_logger_unref(logger);
        }

        /* execvpe only returns if there is an error, otherwise the current process
         * image is replaced with a new process */
        gint returnValue = execvpe(arglist[0], arglist, envlist);

        /* cleanup */
        if(envlist) {
            g_strfreev(envlist);
        }
        if(arglist) {
            g_strfreev(arglist);
        }

        critical("** Error %i while re-launching shadow process: %s", returnValue, g_strerror(returnValue));
        return EXIT_FAILURE;
    }

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
    shadowMaster = master_new(options);

    if(shadowMaster) {
        /* run the simulation */
        returnCode = master_run(shadowMaster);

        /* cleanup */
        master_free(shadowMaster);
        shadowMaster = NULL;
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

    /* parse the options from the command line */
    gchar* cmds = g_strjoinv(" ", argv);
    gchar** cmdv = g_strsplit(cmds, " ", 0);
    g_free(cmds);
    Options* options = options_new(argc, cmdv);
    g_strfreev(cmdv);
    if(!options) {
        return EXIT_FAILURE;
    }


    // If we are just printing the version or running a cleanup+exit,
    // then print the version, cleanup if requested, and exit with success.
    if (options_doRunPrintVersion(options) ||
        options_shouldExitAfterShmCleanup(options)) {
        g_printerr("%s running GLib v%u.%u.%u and IGraph v%s\n%s\n",
                SHADOW_VERSION_STRING,
                (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
#if defined(IGRAPH_VERSION)
                IGRAPH_VERSION,
#else
                "(n/a)",
#endif
                SHADOW_INFO_STRING);

        if (options_shouldExitAfterShmCleanup(options)) {
            shmemcleanup_tryCleanup(false);
        }

        options_free(options);
        return EXIT_SUCCESS;
    }

    /* start up the logging subsystem to handle all future messages */
    ShadowLogger* shadowLogger =
        shadow_logger_new(options_getLogLevel(options));
    shadow_logger_setDefault(shadowLogger);

    /* disable buffering during startup so that we see every message immediately in the terminal */
    shadow_logger_setEnableBuffering(shadowLogger, FALSE);

    // before we run the simluation, clean up any orphaned shared memory
    shmemcleanup_tryCleanup(true);

    gint returnCode = _main_helper(options);

    options_free(options);
    ShadowLogger* logger = shadow_logger_getDefault();
    if(logger) {
        shadow_logger_setDefault(NULL);
        shadow_logger_unref(logger);
    }

    g_printerr("** Stopping Shadow, returning code %i (%s)\n", returnCode, (returnCode == 0) ? "success" : "error");
    return returnCode;
}
