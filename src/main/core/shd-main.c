/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <unistd.h>

#include <elf.h>
#include <link.h>
#include <sys/types.h>

#include "shadow.h"

static Master* shadowMaster;

#define INTERPOSELIBSTR "libshadow-interpose.so"

static gchar* _main_getRPath() {
    const ElfW(Dyn) *dyn = _DYNAMIC;
    const ElfW(Dyn) *rpath = NULL;
    const gchar *strtab = NULL;
    for (; dyn->d_tag != DT_NULL; ++dyn) {
        if (dyn->d_tag == DT_RPATH) {
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

static gboolean _main_isValidPathToPreloadLib(const gchar* path) {
    if(path) {
        gboolean isAbsolute = g_path_is_absolute(path);
        gboolean exists = g_file_test(path, G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS);
        gboolean isShadowInterpose = g_str_has_suffix(path, INTERPOSELIBSTR);

        if(isAbsolute && exists && isShadowInterpose) {
            return TRUE;
        }
    }

    return FALSE;
}

static gchar* _main_searchColonStringForPreload(const gchar* str) {
    GString* candidate = NULL;

    if(str != NULL) {
        gchar** tokens = g_strsplit(str, ":", 0);

        for(gint i = 0; tokens[i] != NULL; i++) {
            candidate = g_string_new(tokens[i]);
            g_string_append(candidate, "/"INTERPOSELIBSTR);

            if(_main_isValidPathToPreloadLib(candidate->str)) {
                break;
            } else {
                g_string_free(candidate, TRUE);
                candidate = NULL;
            }
        }

        g_strfreev(tokens);
    }

    return (candidate != NULL) ? g_string_free(candidate, FALSE) : NULL;
}

static gboolean _main_loadShadowPreload(Options* options) {
    const gchar* preloadOptionStr = options_getPreloadString(options);

    /* clear error message */
    dlerror();

    /* open the shadow preload lib as a preload and in the base program namespace */
    // TODO FIXME this line needs updating for the correct flag(s)
    // also, do we need to save/check the return value?
    void* handle = NULL;
    //handle = dlmopen(LM_ID_BASE, proc->plugin.preloadPath->str, RTLD_PRELOAD);

    const gchar* errorMessage = dlerror();

    if(handle && !errorMessage) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gchar* _main_getPreloadOptionValue(Options* options, Configuration* config) {
    /* return the shadow command line option value string that we should use to specify the path
     * to the shadow preload library. here, our order of preference is:
     *   1. existing "--preload=" option value
     *   2. the 'preload' attribute value of the 'shadow' element in shadow.config.xml
     *   3. the LD_PRELOAD value
     *   4. as a last hope, try looking in RPATH since shadow is built with one
     * if we can't find a path in any of these, return NULL.
     */

    /* 1. */

    const gchar* preloadOptionStr = options_getPreloadString(options);
    if(_main_isValidPathToPreloadLib(preloadOptionStr)) {
        return g_strdup(preloadOptionStr);
    }

    /* 2. */

    gchar* preloadConfigStr = NULL;
    ConfigurationShadowElement* element = configuration_getShadowElement(config);
    if(element && element->preloadPath.isSet) {
        gchar* path = element->preloadPath.string->str;
        if(_main_isValidPathToPreloadLib(path)) {
            return g_strdup(path);
        }
    }

    /* 3. */

    const gchar* preloadEnvStr = g_getenv("LD_PRELOAD");
    gchar* preloadPath = _main_searchColonStringForPreload(preloadEnvStr);

    if(preloadPath && _main_isValidPathToPreloadLib(preloadPath)) {
        return preloadPath;
    }

    /* 4. */

    gchar* rpathStr = _main_getRPath();
    preloadPath = _main_searchColonStringForPreload(rpathStr);
    g_free(rpathStr);

    if(preloadPath && _main_isValidPathToPreloadLib(preloadPath)) {
        return preloadPath;
    }

    return NULL;
}

static gchar* _main_replacePreloadArgument(const gchar** argv, const gchar* preloadArgValue) {
    /* update the command line for our preload library */
    GString* commandBuffer = g_string_new(argv[0]);
    for(gint i = 1; argv != NULL && argv[i] != NULL; i++) {
        /* use -1 to search the entire string */
        if(g_strstr_len(argv[i], -1, "--preload=")) {
            /* skip this key=value string */
        } else if(g_strstr_len(argv[i], -1, "-p")) {
            /* skip this key, and also the next arg which is the value */
            i++;
        } else {
            g_string_append_printf(commandBuffer, " %s", argv[i]);
        }
    }

    /* now add back in the preload option */
    g_string_append_printf(commandBuffer, " --preload=%s", preloadArgValue);

    return g_string_free(commandBuffer, FALSE);
}

static gboolean _main_verifyStaticTLS() {
    const gchar* ldStaticTLSValue = g_getenv("LD_STATIC_TLS_EXTRA");
    if(!ldStaticTLSValue) {
        /* LD_STATIC_TLS_EXTRA contains nothing */
        return FALSE;
    }

    guint64 tlsSize = g_ascii_strtoull(ldStaticTLSValue, NULL, 10);
    if(tlsSize > 1024) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gchar* _main_getStaticTLSValue(Options* options, Configuration* config) {
    // TODO we should compute the actual size we will need based on the shadow.config.xml file
    // that involves loading all plugin libs and the corresponding preload libs (if any)
    // and computing how many nodes need to run each of those plugins to compute total size
    // see src/test/dynlink for code that does this
    return g_strdup("102400");
}

static gint _main_spawnShadow(gchar** argv, gchar** envlist) {
    Logger* logger = logger_getDefault();
    if(logger) {
        logger_unref(logger);
    }
    /* execvpe only returns if there is an error replacing the process image */
    return execvpe(argv[0], argv, envlist);
}

static gint _main_spawnShadowWithValgrind(gchar** argv, gchar** envlist) {
    gchar* args = g_strjoinv(" ", argv);
    GString* newargvBuffer = g_string_new(args);
    g_free(args);

    g_string_prepend(newargvBuffer,
        "valgrind --leak-check=full --show-reachable=yes --track-origins=yes --trace-children=yes --log-file=shadow-valgrind-%p.log --error-limit=no ");
    gchar** newargv = g_strsplit(newargvBuffer->str, " ", 0);
    g_string_free(newargvBuffer, TRUE);

    gboolean success = _main_spawnShadow(newargv, envlist);
    g_strfreev(newargv);
    return success;
}

static gint _main_relaunch(Options* options, gchar** argv) {
    const GString* fileName = options_getInputXMLFilename(options);
    GString* file = utility_getFileContents(fileName->str);
    Configuration* config = configuration_new(options, file);
    g_string_free(file, TRUE);
    if(!config) {
        return -1;
    }

    /* check if we need to run valgrind */
    gboolean runValgrind = options_doRunValgrind(options);

    gchar* preloadArgValue = _main_getPreloadOptionValue(options, config);
    gchar* staticTLSValue = _main_getStaticTLSValue(options, config);

    gchar** envlist = g_get_environ();

    /* keep track that we are relaunching shadow */
    envlist = g_environ_setenv(envlist, "SHADOW_SPAWNED", "TRUE", 1);
    /* make sure LD_PRELOAD is empty to prevent any possible init errors.
     * we will load the preload library at runtime after we relaunch */
//    envlist = g_environ_setenv(envlist, "LD_PRELOAD", "", 1); // uncomment this once we can load the preload lib correctly
    envlist = g_environ_setenv(envlist, "LD_PRELOAD", preloadArgValue, 1);
    /* compute the proper TLS size we need for dlmopen()ing all of the plugins */
    envlist = g_environ_setenv(envlist, "LD_STATIC_TLS_EXTRA", staticTLSValue, 0);

    /* valgrind env */
    if(runValgrind) {
        envlist = g_environ_setenv(envlist, "G_DEBUG", "gc-friendly", 0);
        envlist = g_environ_setenv(envlist, "G_SLICE", "always-malloc", 0);
    }

    /* The following can be used to add internal GLib memory validation that
     * will abort the program if it finds an error. This is useful outside
     * of the valgrind context, as otherwise valgrind will complain about
     * the implementation of the GLib validator.
     * e.g. $ G_SLICE=debug-blocks shadow --file
     *
     * envlist = g_environ_setenv(envlist, "G_SLICE", "debug-blocks", 0);
     */

    gchar* cmds = _main_replacePreloadArgument((const gchar**)argv, (const gchar*)preloadArgValue);
    gchar** cmdv = g_strsplit(cmds, " ", 0);

    gint returnValue = 0;
    if(runValgrind) {
        returnValue = _main_spawnShadowWithValgrind(cmdv, envlist);
    } else {
        returnValue = _main_spawnShadow(cmdv, envlist);
    }

    /* cleanup */
    if(preloadArgValue) {
        g_free(preloadArgValue);
    }
    if(staticTLSValue) {
        g_free(staticTLSValue);
    }
    if(envlist) {
        g_strfreev(envlist);
    }
    if(cmdv) {
        g_strfreev(cmdv);
    }
    if(cmds) {
        g_free(cmds);
    }

    return returnValue;
}

static gboolean _main_doVersionsPass() {
    /* check the compiled GLib version */
    if (!GLIB_CHECK_VERSION(2, 32, 0)) {
        g_printerr("** GLib version 2.32.0 or above is required but Shadow was compiled against version %u.%u.%u\n",
            (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return FALSE;
    }

    if(GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION == 40) {
        g_printerr("** You compiled against GLib version %u.%u.%u, which has bugs known to break Shadow. Please update to a newer version of GLib.\n",
                    (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return FALSE;
    }

    /* check the that run-time GLib matches the compiled version */
    const gchar* mismatch = glib_check_version(GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
    if(mismatch) {
        g_printerr("** The version of the run-time GLib library (%u.%u.%u) is not compatible with the version against which Shadow was compiled (%u.%u.%u). GLib message: '%s'\n",
        glib_major_version, glib_minor_version, glib_micro_version,
        (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
        mismatch);
        return FALSE;
    }

    /* everything passed */
    return TRUE;
}

static gboolean _main_checkRuntimeEnvironment(Options* options) {
    /* make sure we have initialized static tls */
    if(!_main_verifyStaticTLS()) {
        error("** Shadow Setup Check Failed: LD_STATIC_TLS_EXTRA does not contain a nonzero value");
        return FALSE;
    }

    /* make sure we have the shadow preload lib */
    if(!_main_isValidPathToPreloadLib(options_getPreloadString(options))) {
        error("** Shadow Setup Check Failed: cannot find absolute path to "INTERPOSELIBSTR"");
        return FALSE;
    }

    /* now load the preload library into shadow's namespace */
    // TODO add this check once we can load the preload lib at runtime
//    if(!_main_loadShadowPreload(options)) {
//        error("** Shadow Setup Check Failed: unable to load preload library");
//        return FALSE;
//    }

    /* tell the preload lib we are ready for action */
    extern int interposer_setShadowIsLoaded(int);
    int interposerResult = interposer_setShadowIsLoaded(1);

    if(interposerResult != 0) {
        /* it was not intercepted, meaning our preload library is not set up properly */
        error("** Shadow Setup Check Failed: preload library is not correctly interposing functions");
        return FALSE;
    }

    return TRUE;
}

static Options* _main_parseOptions(gint argc, gchar* argv[]) {
    gchar* cmds = g_strjoinv(" ", argv);
    gchar** cmdv = g_strsplit(cmds, " ", 0);
    Options* options = options_new(argc, cmdv);
    g_free(cmds);
    g_strfreev(cmdv);
    return options;
}

gint shadow_main(gint argc, gchar* argv[]) {
    if(!_main_doVersionsPass()) {
        return -1;
    }

    Options* options = _main_parseOptions(argc, argv);
    if(!options) {
        return -1;
    }

    if(options_doRunPrintVersion(options)) {
        g_printerr("%s running GLib v%u.%u.%u and IGraph v%s\n%s\n",
                SHADOW_VERSION_STRING,
                (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
#if defined(IGRAPH_VERSION)
                IGRAPH_VERSION,
#else
                "(n/a)",
#endif
                SHADOW_INFO_STRING);
        options_free(options);
        return 0;
    }

    Logger* shadowLogger = logger_new(options_getLogLevel(options));
    logger_setDefault(shadowLogger);
    logger_setEnableBuffering(shadowLogger, FALSE);

    if(g_getenv("SHADOW_SPAWNED") == NULL) {
        /* setup our required environment and relaunch */
        gint returnValue = _main_relaunch(options, argv);
        error("** Error %i while re-launching shadow process: %s", returnValue, g_strerror(returnValue));
        return -1;
    }

    if(!_main_checkRuntimeEnvironment(options)) {
        return -1;
    }

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
    g_printerr("** %s\n", startupStr);
    g_free(startupStr);

    message(SHADOW_INFO_STRING);
    message("args=%s", options_getArgumentString(options));

    gchar** envlist = g_get_environ();
    for(gint i = 0; envlist != NULL && envlist[i] != NULL; i++) {
        info("env: %s", envlist[i]);
    }
    g_strfreev(envlist);

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

    message("log message buffering is enabled for efficiency");
    logger_setEnableBuffering(shadowLogger, TRUE);

    if(shadowMaster) {
        /* run the simulation */
        returnCode = master_run(shadowMaster);
        /* cleanup */
        master_free(shadowMaster);
        shadowMaster = NULL;
    }

    message("%s simulation was shut down cleanly", SHADOW_VERSION_STRING);

    options_free(options);
    logger_setDefault(NULL);
    logger_unref(shadowLogger);

    g_printerr("** Shadow returning code %i (%s)\n", returnCode, (returnCode == 0) ? "success" : "error");
    return returnCode;
}
