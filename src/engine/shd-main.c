/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

#include <elf.h>
#include <link.h>

static Master* shadowMaster;

#define INTERPOSELIBSTR "libshadow-interpose.so"

static gboolean _main_checkPreloadEnvironment(gchar** envlist) {
    /* we better have preloaded libshadow_preload.so */
    const gchar* ldPreloadValue = g_environ_getenv(envlist, "LD_PRELOAD");
    if(!ldPreloadValue) {
        /* LD_PRELOAD contains nothing */
        return FALSE;
    }

    gboolean found = FALSE;
    gchar** tokens = g_strsplit(ldPreloadValue, ":", 0);

    for(gint i = 0; !found && tokens[i] != NULL; i++) {
        gboolean isAbsolute = g_path_is_absolute(tokens[i]);
        gboolean exists = g_file_test(tokens[i], G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS);
        gboolean isShadowInterpose = g_str_has_suffix(tokens[i], INTERPOSELIBSTR);

        if(isAbsolute && exists && isShadowInterpose) {
            found = TRUE;
        }
    }

    g_strfreev(tokens);

    return found;
}

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

static gboolean _main_appendPathIfValid(GString* preloadBuffer, const gchar* path) {
    gboolean isAbsolute = g_path_is_absolute(path);
    gboolean exists = g_file_test(path, G_FILE_TEST_IS_REGULAR|G_FILE_TEST_EXISTS);

    if(isAbsolute && exists) {
        if(preloadBuffer->len > 0) {
            g_string_append_printf(preloadBuffer, ":%s", path);
        } else {
            g_string_append_printf(preloadBuffer, "%s", path);
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

static gchar** _main_getSpawnEnviroment(const gchar* preloadHint, gboolean valgrind) {
    gchar** envlist = g_get_environ();

    /* get whatever might be currently in the env */
    GString* preloadBuffer = g_string_new(g_environ_getenv(envlist, "LD_PRELOAD"));

    /* add all valid preload hints */
    if(preloadHint) {
        gchar** tokens = g_strsplit(preloadHint, ":", 0);
        for(gint i = 0; tokens[i] != NULL; i++) {
            _main_appendPathIfValid(preloadBuffer, tokens[i]);
        }
        g_strfreev(tokens);
    }

    if(preloadBuffer->str) {
        envlist = g_environ_setenv(envlist, "LD_PRELOAD", preloadBuffer->str, 1);
    }

    /* if we still have not found shadow interpose, lets add one if we can find it in rpath */
    if(!_main_checkPreloadEnvironment(envlist)) {
        gchar* rpathStr = _main_getRPath();
        gchar** tokens = g_strsplit(rpathStr, ":", 0);

        for(gint i = 0; tokens[i] != NULL; i++) {
            GString* candidate = g_string_new(tokens[i]);
            g_string_append(candidate, "/"INTERPOSELIBSTR);
            gboolean success = _main_appendPathIfValid(preloadBuffer, candidate->str);
            g_string_free(candidate, TRUE);
            if(success) {
                break;
            }
        }

        g_strfreev(tokens);
        g_free(rpathStr);
    }

    if(preloadBuffer->str) {
        envlist = g_environ_setenv(envlist, "LD_PRELOAD", preloadBuffer->str, 1);
    }
    g_string_free(preloadBuffer, TRUE);

    /* valgrind env */
    if(valgrind) {
        envlist = g_environ_setenv(envlist, "G_DEBUG", "gc-friendly", 0);
        envlist = g_environ_setenv(envlist, "G_SLICE", "always-malloc", 0);
    }

    /* The following can be used to add internal GLib memory validation that
     * will abort the program if it finds an error. This is useful outside
     * of the valgrind context, as otherwise valgrind will complain about
     * the implementation of the GLib validator.
     * e.g. $ G_SLICE=debug-blocks shadow --file
     * envlist = g_environ_setenv(envlist, "G_SLICE", "debug-blocks", 0);
     */

    envlist = g_environ_setenv(envlist, "SHADOW_SPAWNED", "TRUE", 1);
    return envlist;
}

static gboolean _main_spawnShadow(gchar** argv, gchar** envlist, gint* exitStatus, GError** err) {
    GSpawnFlags sf = G_SPAWN_SEARCH_PATH|G_SPAWN_CHILD_INHERITS_STDIN;
    return g_spawn_sync(NULL, argv, envlist, sf, NULL, NULL, NULL, NULL, exitStatus, err);
}

static gboolean _main_spawnShadowWithValgrind(gchar** argv, gchar** envlist, gint* exitStatus, GError** err) {
    gchar* args = g_strjoinv(" ", argv);
    GString* newargvBuffer = g_string_new(args);
    g_free(args);

    g_string_prepend(newargvBuffer,
        "valgrind --leak-check=full --show-reachable=yes --track-origins=yes --trace-children=yes --log-file=shadow-valgrind-%p.log --error-limit=no ");
    gchar** newargv = g_strsplit(newargvBuffer->str, " ", 0);
    g_string_free(newargvBuffer, TRUE);

    gboolean success = _main_spawnShadow(newargv, envlist, exitStatus, err);
    g_strfreev(newargv);
    return success;
}

gint shadow_main(gint argc, gchar* argv[]) {
    /* check the compiled GLib version */
    if (!GLIB_CHECK_VERSION(2, 32, 0)) {
        g_printerr("** GLib version 2.32.0 or above is required but Shadow was compiled against version %u.%u.%u\n",
            (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return -1;
    }

    if(GLIB_MAJOR_VERSION == 2 && GLIB_MINOR_VERSION == 40) {
        g_printerr("** You compiled against GLib version %u.%u.%u, which has bugs known to break Shadow. Please update to a newer version of GLib.\n",
                    (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION);
        return -1;
    }

    /* check the that run-time GLib matches the compiled version */
    const gchar* mismatch = glib_check_version(GLIB_MAJOR_VERSION, GLIB_MINOR_VERSION, GLIB_MICRO_VERSION);
    if(mismatch) {
        g_printerr("** The version of the run-time GLib library (%u.%u.%u) is not compatible with the version against which Shadow was compiled (%u.%u.%u). GLib message: '%s'\n",
        glib_major_version, glib_minor_version, glib_micro_version,
        (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
        mismatch);
        return -1;
    }

    /* setup configuration - this fails and aborts if invalid */
    gchar* cmds = g_strjoinv(" ", argv);
    gchar** cmdv = g_strsplit(cmds, " ", 0);
    Configuration* config = configuration_new(argc, cmdv);
    g_free(cmds);
    g_strfreev(cmdv);
    if(!config) {
        /* incorrect options given */
        return -1;
    } else if(config->printSoftwareVersion) {
        g_printerr("%s running GLib v%u.%u.%u and IGraph v%s\n%s\n",
                SHADOW_VERSION_STRING,
                (guint)GLIB_MAJOR_VERSION, (guint)GLIB_MINOR_VERSION, (guint)GLIB_MICRO_VERSION,
#if defined(IGRAPH_VERSION)
                IGRAPH_VERSION,
#else
                "(n/a)",
#endif
                SHADOW_INFO_STRING);
        configuration_free(config);
        return 0;
    }

    /* check environment for LD_PRELOAD */
    gchar** envlist = g_get_environ();
    gboolean preloadSuccess = _main_checkPreloadEnvironment(envlist);
    g_strfreev(envlist);
    gboolean respawned = g_getenv("SHADOW_SPAWNED") != NULL ? TRUE : FALSE;

    if(respawned) {
        /* if shadow already respawned once and LD_PRELOAD still isnt correct,
         * then the user will need to provide the correct path */
        if(!preloadSuccess) {
            g_printerr("** Environment Check Failed: LD_PRELOAD does not contain an absolute path to "INTERPOSELIBSTR"\n");
            return -1;
        }
        /* NOTE: we ignore valgrind and preload options during the respawn */
    } else {
        /* if preload is not set, or the user added a preload library,
         * or we are going to run valgrind, we need to respawn */
        if(config->preloads || config->runValgrind || !preloadSuccess) {
            gchar** envlist = _main_getSpawnEnviroment(config->preloads, config->runValgrind);
            gchar* cmds = g_strjoinv(" ", argv);
            gchar** cmdv = g_strsplit(cmds, " ", 0);
            GError* error = NULL;
            gint exitStatus = 0;

            gboolean spawnSuccess = config->runValgrind ?
                    _main_spawnShadowWithValgrind(cmdv, envlist, &exitStatus, &error) :
                    _main_spawnShadow(cmdv, envlist, &exitStatus, &error);

            g_free(cmds);
            g_strfreev(cmdv);
            g_strfreev(envlist);

            if(!spawnSuccess) {
                g_printerr("** Error %i while re-spawning shadow process: %s\n", error->code, error->message);
                return -1;
            }

            /* child was run */
            return (exitStatus == 0) ? 0 : -1;
        }
    }

    utility_assert(preloadSuccess);

    /* tell the preaload lib we are ready for action */
    extern void interposer_setShadowIsLoaded();
    interposer_setShadowIsLoaded();

    /* allocate and initialize our main simulation driver */
    gint returnCode = 0;
    shadowMaster = master_new(config);
    if(shadowMaster) {
        /* run the simulation */
        returnCode = master_run(shadowMaster);
        /* cleanup */
        master_free(shadowMaster);
        shadowMaster = NULL;
    }

    configuration_free(config);
    g_printerr("** shadow returning code %i (%s)\n", returnCode, (returnCode == 0) ? "success" : "error");
    return returnCode;
}
