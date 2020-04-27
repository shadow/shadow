/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <elf.h>
#include <glib.h>
#include <igraph_version.h>
#include <link.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "external/elf-loader/dl.h"
#include "main/core/logger/shadow_logger.h"
#include "main/core/master.h"
#include "main/core/support/configuration.h"
#include "main/core/support/options.h"
#include "main/utility/utility.h"
#include "shd-config.h"
#include "support/logger/logger.h"

static Master* shadowMaster;

#define INTERPOSELIBSTR "libshadow-interpose.so"

static gchar* _main_getRPath() {
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

static gboolean _main_loadShadowPreload(Options* options) {
    const gchar* preloadOptionStr = options_getPreloadString(options);

    /* clear error message */
    dlerror();

    /* open the shadow preload lib as a preload and in the base program namespace */
    void* handle = dlmopen(LM_ID_BASE, preloadOptionStr, RTLD_LAZY|RTLD_PRELOAD);

    const gchar* errorMessage = dlerror();

    if(handle && !errorMessage) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gboolean _main_verifyStaticTLS() {
    const gchar* ldStaticTLSValue = g_getenv("LD_STATIC_TLS_EXTRA");
    if(!ldStaticTLSValue) {
        /* LD_STATIC_TLS_EXTRA contains nothing */
        return FALSE;
    }

    guint64 tlsSize = g_ascii_strtoull(ldStaticTLSValue, NULL, 10);
    if(tlsSize >= 1024) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static gulong _main_computeLoadSize(Lmid_t lmid, const gchar* pluginPath, const gchar* preloadPath, gint dlmopenFlags) {
    gulong tlsSizeStart = 0, tlsSizeEnd = 0, tlsSizePerLoad = 0;
    gpointer handle1 = NULL, handle2 = NULL;
    gint result = 0;

    debug("computing static TLS size needed for library at path '%s' and preload at path '%s'",
            pluginPath, preloadPath ? preloadPath : "NULL");

    /* clear error */
    dlerror();

    /* get the initial TLS size used */
    result = dlinfo(RTLD_DI_STATIC_TLS_SIZE_DOESNT_REQUIRE_HANDLE, RTLD_DI_STATIC_TLS_SIZE, &tlsSizeStart);

    if (result != 0) {
        warning("error in dlinfo() while computing TLS start size, dlerror is '%s'", dlerror());
        goto err;
    }

    /* open the plugin library in the requested lmid */
    handle1 = dlmopen(lmid, pluginPath, dlmopenFlags);

    if(handle1 == NULL) {
        warning("error in plugin dlmopen() while computing TLS size, dlerror is '%s'", dlerror());
        goto err;
    }

    if(preloadPath) {
        /* get the LMID so we can load it in the same namespace as the plugin */
        Lmid_t prev_lmid = 0;
        result = dlinfo(handle1, RTLD_DI_LMID, &prev_lmid);

        if(result != 0) {
            warning("dlinfo() failed when querying for previous LMID: %s", dlerror());
            goto err;
        }

        handle2 = dlmopen(prev_lmid, preloadPath, dlmopenFlags|RTLD_INTERPOSE);
        if(handle2 == NULL) {
            warning("error in preload dlmopen() while computing TLS size, dlerror is '%s'", dlerror());
            goto err;
        }
    }

    result = dlinfo(RTLD_DI_STATIC_TLS_SIZE_DOESNT_REQUIRE_HANDLE, RTLD_DI_STATIC_TLS_SIZE, &tlsSizeEnd);

    if (result != 0) {
        warning("error in dlinfo() while computing TLS end size, dlerror is '%s'", dlerror());
        goto err;
    }

// TODO close the handles to clean up, after elf loader supports interleaving dlopens and dlcloses
//    dlclose(handle1);
//    dlclose(handle2);

    tlsSizePerLoad = (tlsSizeEnd - tlsSizeStart);

    /* log the result */
    GString* msg = g_string_new(NULL);
    g_string_printf(msg, "we need %lu bytes of static TLS per load of namespace with library at path '%s'", tlsSizePerLoad, pluginPath);
    if(preloadPath) {
        g_string_append_printf(msg, " and preload at path '%s'", preloadPath);
    }
    message("%s", msg->str);
    g_string_free(msg, TRUE);

    /* make sure we dont return 0 when successful */
    if(tlsSizePerLoad < 1) {
        tlsSizePerLoad = 1;
    }
    return tlsSizePerLoad;

err:
// TODO close the handles to clean up, after elf loader supports interleaving dlopens and dlcloses
//    if(handle1) {
//        dlclose(handle1);
//    }
//    if(handle2) {
//        dlclose(handle2);
//    }
    return 0;
}

static gulong _main_computeProcessLoadSize(const gchar* pluginPath, const gchar* preloadPath) {
    return _main_computeLoadSize(LM_ID_NEWLM, pluginPath, preloadPath, RTLD_LAZY|RTLD_LOCAL);
}

static gulong _main_computePreloadLoadSize(const gchar* libraryPath) {
    return _main_computeLoadSize(LM_ID_BASE, libraryPath, NULL, RTLD_LAZY|RTLD_PRELOAD);
}

typedef struct _TLSCountingState {
    Configuration* config;
    GQueue* allHosts;
    GHashTable* cachedProcessTLSSize;
    guint currentHostQuantity;
    gulong tlsSizeTotal;
    gulong numCacheHits;
    gulong numCacheMisses;
} TLSCountingState;

static gulong _main_getTLSUsedByProcess(TLSCountingState* state,
        ConfigurationPluginElement* pluginElement, ConfigurationPluginElement* preloadElement) {
    /* figure out how much TLS is used to load both into the same namespace.
     * only do the actual dlmopens once and then cache the result, so that we
     * don't repeat the loads for every host. */

    utility_assert(pluginElement);
    utility_assert(pluginElement->id.isSet && pluginElement->id.string);
    utility_assert(pluginElement->path.isSet && pluginElement->path.string);
    if(preloadElement) {
        utility_assert(preloadElement->id.isSet && preloadElement->id.string);
        utility_assert(preloadElement->path.isSet && preloadElement->path.string);
    }

    /* the id for the cache map is the concatenation of the plugin and preload ids */
    gchar* cacheID = NULL;
    gint result = asprintf(&cacheID, "%s-%s", pluginElement->id.string->str,
            preloadElement ? preloadElement->id.string->str : "NULL");
    if(result < 0) {
        return 0;
    }

    /* first check if we can get the size from the cache */
    gulong* cachedSizePointer = NULL;
    cachedSizePointer = g_hash_table_lookup(state->cachedProcessTLSSize, cacheID);
    if(cachedSizePointer != NULL) {
        /* cache hit, we can avoid doing the dlmopens */
        state->numCacheHits++;
        return *cachedSizePointer;
    } else {
        /* cache miss, do the dlmopens and cache the result */
        state->numCacheMisses++;
        cachedSizePointer = g_new0(gulong, 1);
        *cachedSizePointer = _main_computeProcessLoadSize(pluginElement->path.string->str,
            preloadElement ? preloadElement->path.string->str : NULL);
        g_hash_table_replace(state->cachedProcessTLSSize, cacheID, cachedSizePointer);
        return *cachedSizePointer;
    }
}

static void _main_countTLSProcessCallback(ConfigurationProcessElement* processElement, TLSCountingState* state) {
    utility_assert(processElement && state);

    /* each process must have a plugin, and can optionally have a preload lib */
    ConfigurationPluginElement* pluginElement = NULL;
    ConfigurationPluginElement* preloadElement = NULL;

    if(processElement && processElement->plugin.isSet && processElement->plugin.string) {
        gchar* pluginID = processElement->plugin.string->str;
        pluginElement = configuration_getPluginElementByID(state->config, pluginID);
    }

    if(processElement && processElement->preload.isSet && processElement->preload.string) {
        gchar* preloadID = processElement->preload.string->str;
        preloadElement = configuration_getPluginElementByID(state->config, preloadID);
    }

    /* get the TLS used by this process and store total needed by this host element */
    gulong tlsSizePerProcess = _main_getTLSUsedByProcess(state, pluginElement, preloadElement);
    if(tlsSizePerProcess == 0) {
        warning("skipping process with plugin '%s' at path '%s' "
                "and preload '%s' at path '%s' "
                "when computing total needed static TLS size",
                pluginElement ? pluginElement->id.string->str : "n/a",
                pluginElement ? pluginElement->path.string->str : "n/a",
                preloadElement ? preloadElement->id.string->str : "n/a",
                preloadElement ? preloadElement->path.string->str : "n/a");
    } else {
        state->tlsSizeTotal += (tlsSizePerProcess * state->currentHostQuantity);
    }
}

static void _main_countTLSHostCallback(ConfigurationHostElement* hostElement, TLSCountingState* state) {
    utility_assert(hostElement && state);

    if(hostElement && hostElement->processes) {
        /* make sure to get the new quantity set of each host */
        state->currentHostQuantity = hostElement->quantity.isSet ? ((guint)hostElement->quantity.integer) : 1;

        /* now figure out the plugins that each process is using */
        g_queue_foreach(hostElement->processes, (GFunc)_main_countTLSProcessCallback, state);
    }
}

static gchar* _main_getStaticTLSValue(Options* options, Configuration* config, gchar* preloadArgValue) {
    /*
     * compute the actual static TLS size we need based on the libraries specified in the
     * shadow.config.xml file and our environment. this involves finding all of the plugins
     * and preloads that we need, and counting how many nodes load each of those libraries.
     */

    TLSCountingState* state = g_new0(TLSCountingState, 1);
    state->config = config;
    state->allHosts = configuration_getHostElements(config);
    state->cachedProcessTLSSize = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    /* for each lib, we go through each node and find each application that uses that lib */
    g_queue_foreach(state->allHosts, (GFunc)_main_countTLSHostCallback, state);

    /* now count the size required to load the global shadow preload library */
    gulong tlsSizePerLoad = _main_computePreloadLoadSize(preloadArgValue);
    if(tlsSizePerLoad == 0) {
        warning("skipping shadow global preload lib at path '%s' when computing total needed static TLS size",
                preloadArgValue);
    } else {
        state->tlsSizeTotal += tlsSizePerLoad;
    }

    /* make sure we want to use at least 1 KiB */
    if(state->tlsSizeTotal < 1024) {
        state->tlsSizeTotal = 1024;
    }

    message("finished checking TLS size required for %u hosts; used dlmopen for %lu namespaces and cache for %lu namespaces",
            state->allHosts->length, state->numCacheMisses, state->numCacheHits);

    GString* sbuf = g_string_new(NULL);
    g_string_printf(sbuf, "%lu", state->tlsSizeTotal);
    g_hash_table_destroy(state->cachedProcessTLSSize);
    g_free(state);
    return g_string_free(sbuf, FALSE);
}

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

    /* check if we still need to setup our required environment and relaunch */
    if(g_getenv("SHADOW_SPAWNED") == NULL) {
        message("shadow will automatically adjust environment and relaunch");
        message("loading shadow configuration file");

        /* we need to relaunch.
         * first lets load the config file to help us setup the environment */
        const GString* fileName = options_getInputXMLFilename(options);
        if(!fileName) {
            return EXIT_FAILURE;
        }

        GString* file = utility_getFileContents(fileName->str);
        if(!file) {
            critical("unable to read config file contents");
            return EXIT_FAILURE;
        }

        Configuration* config = configuration_new(options, file);
        g_string_free(file, TRUE);
        file = NULL;
        if(!config) {
            critical("there was a problem parsing the Shadow config file, and we can't run without it");
            return EXIT_FAILURE;
        }

        message("shadow configuration file loaded, parsed, and passed validation");

        /* now start to set up the environment */
        gchar** envlist = g_get_environ();
        GString* commandBuffer = g_string_new(options_getArgumentString(options));

        if(!envlist || !commandBuffer) {
            critical("there was a problem loading existing environment");
            configuration_free(config);
            return EXIT_FAILURE;
        }

        message("setting up LD_PRELOAD environment");

        /* compute the proper LD_PRELOAD value, extract the shadow preload file and
         * set it as a command line argument if needed */
        gchar* preloadArgValue = NULL;
        {
            /* before we restart, we should:
             *  -set the shadow preload lib as a command line arg
             *  -make sure it does not exist in LD_PRELOAD, but otherwise leave LD_PRELOAD in place
             * we need to search for our preload lib. our order of preference follows.
             */

            /* 1. existing "--preload=" option value */

            if(_main_isValidPathToPreloadLib(options_getPreloadString(options))) {
                preloadArgValue = g_strdup(options_getPreloadString(options));
            }

            /* 2. the 'preload' attribute value of the 'shadow' element in shadow.config.xml */

            /* we only need to search if we haven't already found a valid path */
            if(!preloadArgValue) {
                ConfigurationShadowElement* element = configuration_getShadowElement(config);
                if(element && element->preloadPath.isSet) {
                    gchar* path = element->preloadPath.string->str;
                    if(_main_isValidPathToPreloadLib(path)) {
                        preloadArgValue = g_strdup(path);
                    }
                }
            }

            /* 3. the LD_PRELOAD value */

            GString* preloadEnvValueBuffer = NULL;
            /* we always search the env variable and remove existing Shadow preload libs */
            if(g_environ_getenv(envlist, "LD_PRELOAD") != NULL) {
                gchar** tokens = g_strsplit(g_environ_getenv(envlist, "LD_PRELOAD"), ":", 0);

                for(gint i = 0; tokens[i] != NULL; i++) {
                    /* each token in the env variable should be an absolute path */
                    if(_main_isValidPathToPreloadLib(tokens[i])) {
                        /* found a valid path, only save it if we don't have one yet (from options or config) */
                        if(!preloadArgValue) {
                            preloadArgValue = g_strdup(tokens[i]);
                        }
                    } else {
                        /* maintain non-shadow entries */
                        if(preloadEnvValueBuffer) {
                            g_string_append_printf(preloadEnvValueBuffer, ":%s", tokens[i]);
                        } else {
                            preloadEnvValueBuffer = g_string_new(tokens[i]);
                        }
                    }
                }

                g_strfreev(tokens);
            }

            /* 4. the 'environment' attribute of the 'shadow' configuration element in shadow.config.xml */

            /* always go through the 'environment' attribute of the 'shadow' element to pull in any keys defined there */
            {
                ConfigurationShadowElement* element = configuration_getShadowElement(config);
                if(element && element->environment.isSet) {
                    /* entries are split by ';' */
                    gchar** envTokens = g_strsplit(element->environment.string->str, ";", 0);

                    for(gint i = 0; envTokens[i] != NULL; i++) {
                        /* each env entry is key=value, get 2 tokens max */
                        gchar** items = g_strsplit(envTokens[i], "=", 2);

                        gchar* key = items[0];
                        gchar* value = items[1];

                        if(key != NULL && value != NULL) {
                            /* check if the key is LD_PRELOAD */
                            if(!g_ascii_strncasecmp(key, "LD_PRELOAD", 10)) {
                                gchar** preloadTokens = g_strsplit(value, ":", 0);

                                for(gint j = 0; preloadTokens[j] != NULL; j++) {
                                    /* each token in the env variable should be an absolute path */
                                    if(_main_isValidPathToPreloadLib(preloadTokens[j])) {
                                        /* found a valid path, only save it if we don't have one yet (from options or config) */
                                        if(!preloadArgValue) {
                                            preloadArgValue = g_strdup(preloadTokens[j]);
                                        }
                                    } else {
                                        /* maintain non-shadow entries */
                                        if(preloadEnvValueBuffer) {
                                            g_string_append_printf(preloadEnvValueBuffer, ":%s", preloadTokens[j]);
                                        } else {
                                            preloadEnvValueBuffer = g_string_new(preloadTokens[j]);
                                        }
                                    }
                                }

                                g_strfreev(preloadTokens);
                            } else {
                                /* set the key=value pair, but don't overwrite any existing settings */
                                envlist = g_environ_setenv(envlist, key, value, 0);
                            }
                        }

                        g_strfreev(items);
                    }

                    g_strfreev(envTokens);
                }
            }

            /* save away the non-shadow preload libs */
            if(preloadEnvValueBuffer) {
                envlist = g_environ_setenv(envlist, "LD_PRELOAD", preloadEnvValueBuffer->str, 1);
                g_string_free(preloadEnvValueBuffer, TRUE);
                preloadEnvValueBuffer = NULL;
            } else {
                envlist = g_environ_unsetenv(envlist, "LD_PRELOAD");
            }

            /* 5. as a last hope, try looking in RPATH since shadow is built with one */

            /* we only need to search if we haven't already found a valid path */
            if(!preloadArgValue) {
                gchar* rpathStr = _main_getRPath();
                if(rpathStr != NULL) {
                    gchar** tokens = g_strsplit(rpathStr, ":", 0);

                    for(gint i = 0; tokens[i] != NULL; i++) {
                        GString* candidateBuffer = g_string_new(NULL);

                        /* rpath specifies directories, so look inside */
                        g_string_printf(candidateBuffer, "%s/%s", tokens[i], INTERPOSELIBSTR);
                        gchar* candidate = g_string_free(candidateBuffer, FALSE);

                        if(_main_isValidPathToPreloadLib(candidate)) {
                            preloadArgValue = candidate;
                            break;
                        } else {
                            g_free(candidate);
                        }
                    }

                    g_strfreev(tokens);
                }
                g_free(rpathStr);
            }

            /* if we still didn't find our preload lib, that is a user error */
            if(!preloadArgValue) {
                critical("can't find path to %s, did you specify an absolute path to an existing readable file?", INTERPOSELIBSTR);
                configuration_free(config);
                return EXIT_FAILURE;
            }

            /* now that we found the correct path to the preload lib, first remove any possibly
             * incomplete path that might exist in the command line args, and then replace it
             * with the path that we found and verified is correct. */

            {
                /* first remove all preload options */
                gchar** tokens = g_strsplit(commandBuffer->str, " ", 0);
                g_string_free(commandBuffer, TRUE);
                commandBuffer = NULL;

                for(gint i = 0; tokens[i] != NULL; i++) {
                    /* use -1 to search the entire string */
                    if(!g_ascii_strncasecmp(tokens[i], "--preload=", 10)) {
                        /* skip this key=value string */
                    } else if(!g_ascii_strncasecmp(tokens[i], "-p", 2)) {
                        /* skip this key, and also the next arg which is the value */
                        i++;
                    } else {
                        if(commandBuffer) {
                            g_string_append_printf(commandBuffer, " %s", tokens[i]);
                        } else {
                            commandBuffer = g_string_new(tokens[i]);
                        }
                    }
                }

                g_strfreev(tokens);

                /* now add back in the preload option */
                g_string_append_printf(commandBuffer, " --preload=%s", preloadArgValue);
            }

        }

        message("setting up LD_STATIC_TLS_EXTRA environment");

        /* compute the proper TLS size we need for dlmopen()ing all of the plugins,
         * but only do this if the user didn't manually specify a size */
        if(g_environ_getenv(envlist, "LD_STATIC_TLS_EXTRA") == NULL) {
            gchar* staticTLSValue = _main_getStaticTLSValue(options, config, preloadArgValue);
            envlist = g_environ_setenv(envlist, "LD_STATIC_TLS_EXTRA", staticTLSValue, 0);
            message("we need %s total bytes of static TLS storage", staticTLSValue);
            g_free(staticTLSValue);
        }

        /* cleanup unused string */
        if(preloadArgValue) {
            g_free(preloadArgValue);
            preloadArgValue = NULL;
        }

        /* are we running valgrind */
        if(options_doRunValgrind(options)) {
            message("setting up environment for valgrind");

            /* make glib friendlier to valgrind */
            envlist = g_environ_setenv(envlist, "G_DEBUG", "gc-friendly", 0);
            envlist = g_environ_setenv(envlist, "G_SLICE", "always-malloc", 0);

            /* add the valgrind command and some default options */
            g_string_prepend(commandBuffer,
                            "valgrind --leak-check=full --show-reachable=yes --track-origins=yes --trace-children=yes --log-file=shadow-valgrind-%p.log --error-limit=no ");
        } else {
            /* The following can be used to add internal GLib memory validation that
             * will abort the program if it finds an error. This is only useful outside
             * of the valgrind context, as otherwise valgrind will complain about
             * the implementation of the GLib validator.
             * e.g. $ G_SLICE=debug-blocks shadow --file
             *
             * envlist = g_environ_setenv(envlist, "G_SLICE", "debug-blocks", 0);
             */
        }

        /* keep track that we are relaunching shadow */
        envlist = g_environ_setenv(envlist, "SHADOW_SPAWNED", "TRUE", 1);

        gchar* command = g_string_free(commandBuffer, FALSE);
        gchar** arglist = g_strsplit(command, " ", 0);
        g_free(command);

        configuration_free(config);

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

    /* we dont need to relaunch, so we can run the simulation */

    /* make sure we have initialized static tls */
    if(!_main_verifyStaticTLS()) {
        critical("** Shadow Setup Check Failed: LD_STATIC_TLS_EXTRA does not contain a nonzero value");
        return EXIT_FAILURE;
    }

    /* make sure we have the shadow preload lib */
    if(!_main_isValidPathToPreloadLib(options_getPreloadString(options))) {
        critical("** Shadow Setup Check Failed: cannot find absolute path to "INTERPOSELIBSTR"");
        return EXIT_FAILURE;
    }

    /* now load the preload library into shadow's namespace */
    if(!_main_loadShadowPreload(options)) {
        critical("** Shadow Setup Check Failed: unable to load preload library");
        return EXIT_FAILURE;
    }

    /* tell the preload lib we are ready for action */
    extern int interposer_setShadowIsLoaded(int);
    int interposerResult = interposer_setShadowIsLoaded(1);

    if(interposerResult != 0) {
        /* it was not intercepted, meaning our preload library is not set up properly */
        critical("** Shadow Setup Check Failed: preload library is not correctly interposing functions");
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

    /* if they just want the shadow version, print it and exit */
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
        return EXIT_SUCCESS;
    }

    /* start up the logging subsystem to handle all future messages */
    ShadowLogger* shadowLogger = shadow_logger_new(options_getLogLevel(options));
    shadow_logger_setDefault(shadowLogger);

    /* disable buffering during startup so that we see every message immediately in the terminal */
    shadow_logger_setEnableBuffering(shadowLogger, FALSE);

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
