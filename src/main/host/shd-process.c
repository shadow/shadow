/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef __USE_GNU
#define __USE_GNU 1
#endif
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>

#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <malloc.h>
#include <signal.h>
#include <poll.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/syscall.h>
#include <linux/sockios.h>
#include <features.h>
#include <wchar.h>

#include <pthread.h>
#include <rpth.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "dl.h"
//Modified for BLEEP Response Port Error
#include "stdlib.h"

#if defined(FD_SETSIZE)
#if FD_SETSIZE > 1024
#error "FD_SETSIZE is larger than what GNU Pth can handle."
#endif
#endif

#ifndef IOV_MAX
#ifdef UIO_MAXIOV
#define IOV_MAX UIO_MAXIOV
#else
#define IOV_MAX 1024
#endif
#endif

#ifndef O_DIRECT
#define O_DIRECT 040000
#endif

#define PROC_PTH_STACK_SIZE 128*1024

#include "shadow.h"

/**
 * We call this function to run the plugin executable. This is the default
 * symbol name when one isn't specified in the plugin configuration element.
 * A start symbol must exist or the dlsym lookup will fail.
 */
#define PLUGIN_DEFAULT_SYMBOL "main"

/**
 * We call this function to get the location where we should set errno for this program.
 * This symbol must exist or the dlsym lookup will fail.
 */
#define PLUGIN_ERRNOLOC_SYMBOL "__errno_location"

/* Global symbols that plugins *may* define to hook changes in execution control */
#define PLUGIN_POSTLOAD_SYMBOL "__shadow_plugin_load__"
#define PLUGIN_PREUNLOAD_SYMBOL "__shadow_plugin_unload__"
#define PLUGIN_PREENTER_SYMBOL "__shadow_plugin_enter__"
#define PLUGIN_POSTEXIT_SYMBOL "__shadow_plugin_exit__"

/* define function signatures for some plugin functions */
typedef gint (*PluginMainFunc)(int argc, char* argv[]);
typedef void (*PluginHookFunc)(void* uniqueid);
typedef int* (*ErrnoLocationFunc)(void);

typedef int (*PluginSigactionFunc)(int signum, const struct sigaction *restrict action, struct sigaction *restrict oldaction);

typedef void (*PluginExitCallbackFunc)();
typedef void (*PluginExitCallbackArgumentsFunc)(int, void*);

typedef void *(*PthSpawnFunc)(void *);
typedef void (*PthCleanupFunc)(void *);
typedef void (*PthAtForkFunc)(void *);

typedef enum _ProcessContext ProcessContext;
enum _ProcessContext {
    PCTX_NONE, PCTX_SHADOW, PCTX_PLUGIN, PCTX_PTH
};

typedef struct _ProcessExitCallbackData ProcessExitCallbackData;
struct _ProcessExitCallbackData {
    gpointer callback;
    gpointer argument;
    gboolean passArgument;
};

typedef struct _ProcessAtForkCallbackData ProcessAtForkCallbackData;
struct _ProcessAtForkCallbackData {
    Process* proc;
    void (*prepare)(void);
    void (*parent)(void);
    void (*child)(void);
};

typedef struct _ProcessChildData ProcessChildData;
struct _ProcessChildData {
    Process* proc;
    PthSpawnFunc run;
    void* arg;
};

typedef enum _SystemCallType SystemCallType;
enum _SystemCallType {
    SCT_BIND, SCT_CONNECT, SCT_GETSOCKNAME, SCT_GETPEERNAME,
};

struct _Process {
    /* the parent virtual host that this process is running on */
    Host* host;

    /* unique id of the program that this process should run */
    guint processID;
    GString* processName;
    FILE* stdoutFile;
    FILE* stderrFile;

    /* the shadow plugin executable */
    struct {
        GString* name;
        GString* path;
        GString* startSymbol;
        void* handle;
        GString* preloadName;
        GString* preloadPath;

        /* every plug-in needs a main function, which we call to start the virtual process */
        PluginMainFunc main;

        /* these functions allow us to notify the plugin code when we are passing control,
         * they are non-Null only if the plug-in optionally defines the symbols above */
        PluginHookFunc postLibraryLoad;
        PluginHookFunc preLibraryUnload;
        PluginHookFunc preProcessEnter;
        PluginHookFunc postProcessExit;

        /* the sigaction function symbol from inside the plugin namespace */
        PluginSigactionFunc sigaction;

        /* the function that will return the specific location of errno for this plugin */
        ErrnoLocationFunc errnoGetLocation;
        /* tracking when the errno location changed because of TLS migration */
        gboolean errnoGetLocationIsStale;

        /*
         * TRUE from when we've called into plug-in code until the call completes.
         * Note that the plug-in may get back into shadow code during execution, by
         * calling a function that we intercept. isShadowContext distinguishes this.
         */
        gboolean isExecuting;
    } plugin;

    /* the namespace that the plugin, its preloads, and objects loaded in the
     * default namespace during execution are in
     */
    Lmid_t lmid;

    /* the portable thread state this process uses when executing the program */
    pth_gctx_t tstate;
    /* the main fd used to wait for notifications from shadow */
    gint epollfd;

    /* shadow runs in pths 'main' thread */
    pth_t shadowThread;
    /* the shadow thread spawns a child to run the program main function */
    pth_t programMainThread;
    /* any other threads created by the program are auxiliary threads */
    GHashTable* programAuxThreads;

    /*
     * Distinguishes which context we are in. Whenever the flow of execution
     * passes into the plug-in, this is FALSE, and whenever it comes back to
     * shadow, this is TRUE. This is used to determine if we should actually
     * be intercepting functions or not, since we dont want to intercept them
     * if they provide shadow with needed functionality.
     *
     * We must be careful to set this correctly at every boundry (shadowlib,
     * interceptions, etc)
     */
    ProcessContext activeContext;

    /* timer for CPU delay measurements */
    GTimer* cpuDelayTimer;

    /* rlimit of the number of open files, needed by poll */
    gsize fdLimit;

    /* process boot and shutdown variables */
    SimulationTime startTime;
    SimulationTime stopTime;
    GString* arguments;
    gchar** argv;
    gint argc;
    gint returnCode;
    gboolean returnCodeLogged;
    GQueue* atExitFunctions;

    /* other state for pthread interface */
    gint pthread_concurrency;

    /* static buffers */
    struct tm timeBuffer;

    /* to avoid glib recursive log errors */
    GQueue* cachedWarningMessages;

    gint referenceCount;
    MAGIC_DECLARE;
};

static ProcessContext _process_changeContext(Process* proc, ProcessContext from, ProcessContext to) {
    ProcessContext prevContext = PCTX_NONE;
    if(from == PCTX_SHADOW) {
        MAGIC_ASSERT(proc);
        prevContext = proc->activeContext;
        utility_assert(prevContext == from);
        proc->activeContext = to;
    } else if(to == PCTX_SHADOW) {
        prevContext = proc->activeContext;
        proc->activeContext = to;
        MAGIC_ASSERT(proc);
        utility_assert(prevContext == from);
    } else {
        utility_assert(proc);
        utility_assert(proc->activeContext == from);
        prevContext = proc->activeContext;
        proc->activeContext = to;
    }
    return prevContext;
}

static const gchar* _process_getPluginPath(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(proc->plugin.path);
    return proc->plugin.path->str;
}

static const gchar* _process_getPluginName(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(proc->plugin.name);
    return proc->plugin.name->str;
}

static const gchar* _process_getPluginStartSymbol(Process* proc) {
    MAGIC_ASSERT(proc);
    if (!proc->plugin.startSymbol) proc->plugin.startSymbol = g_string_new("mainGo");
    return proc->plugin.startSymbol->str;
}

static const gchar* _process_getName(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(proc->processName->str);
    return proc->processName->str;
}

static void _process_updateErrnoLocation(Process* proc) {
    /* clear dlerror status string */
    dlerror();

    /* search for the location for errno in the calling thread */
    gpointer symbol = dlsym(proc->plugin.handle, PLUGIN_ERRNOLOC_SYMBOL);
    if(symbol) {
        proc->plugin.errnoGetLocation = symbol;
        info("found '%s' at %p", PLUGIN_ERRNOLOC_SYMBOL, symbol);

        /* now that we just did the lookup, the errno location is no longer stale */
        proc->plugin.errnoGetLocationIsStale = FALSE;
    } else {
        const gchar* errorMessage = dlerror();
        critical("dlsym() failed: %s", errorMessage);
        error("unable to find the required function symbol '%s' in plug-in '%s'",
                PLUGIN_ERRNOLOC_SYMBOL, proc->plugin.path->str);
    }
}

static void _process_setErrno(Process* proc, int errnoValue) {
    MAGIC_ASSERT(proc);

    /* if we migrated to a new thread, the old errno location function is stale and
     * we need to find the new location. this is because errno uses TLS, and the location
     * of errno moves after migrating across threads. */
    if(proc->plugin.errnoGetLocationIsStale) {
        _process_updateErrnoLocation(proc);
    }

    if(proc->plugin.errnoGetLocation) {
        int* errnoLocation = proc->plugin.errnoGetLocation();
        if(errnoLocation) {
            *errnoLocation = errnoValue;
        }
    }

    /* this is needed for when pth checks errno */
    errno = errnoValue;
}

static void _process_unloadPlugin(Process* proc) {
    MAGIC_ASSERT(proc);

    if(proc->plugin.handle) {
        if(proc->plugin.preLibraryUnload != NULL) {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
            proc->plugin.preLibraryUnload(proc->plugin.handle);
            _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
        }

        /* clear dlerror status string */
        dlerror();

        if(dlclose(proc->plugin.handle) != 0) {
            const gchar* errorMessage = dlerror();
            warning("dlclose() failed: %s", errorMessage);
            warning("failed closing plugin '%s' at address '%p'", proc->plugin.path->str, proc->plugin.handle);
        } else {
            message("successfully unloaded private plug-in '%s' at address '%p'", proc->plugin.path->str, proc->plugin.handle);
        }
    }

    proc->plugin.handle = NULL;
}

static void _process_pluginSignalHandler(int signum) {
    /* calling abort should handle killing the correct pth thread instead of shadow */
    abort();
}

static void _process_loadPlugin(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(!proc->plugin.handle);

    /*
     * get the plugin handle from the library at filename.
     *
     * @warning only global dlopens are searchable with dlsym
     * we cant use G_MODULE_BIND_LOCAL if we want to be able to lookup
     * functions using dlsym in the plugin itself. if G_MODULE_BIND_LOCAL
     * functionality is desired, then we must require plugins to separate their
     * intercepted functions to a SHARED library, and link the plugin to that.
     *
     * We need a new namespace to keep state for each plugin separate.
     * From the manpage:
     *
     * ```
     * LM_ID_BASE
     * Load the shared object in the initial namespace (i.e., the application's namespace).
     *
     * LM_ID_NEWLM
     * Create  a new namespace and load the shared object in that namespace.  The object
     * must have been correctly linked to reference all of the other shared objects that
     * it requires, since the new namespace is initially empty.
     * ```
     */

    /* set a timer for the loading process */
    GTimer* loadTimer = g_timer_new();

    /* dlmopen may result in plugin constructors getting called, so make sure
     * we make that call from the plugin context. */
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);

    /* clear dlerror status string */
    dlerror();

    /* We need lazy binding here, so that later loads can interpose symbols. */
    proc->plugin.handle = dlmopen(LM_ID_NEWLM, proc->plugin.path->str, RTLD_LAZY|RTLD_GLOBAL);
    const gchar* errorMessage = dlerror();

    _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);

    /* check the load timer */
    gdouble secondsElapsedDuringLoad = g_timer_elapsed(loadTimer, NULL);

    if(proc->plugin.handle) {
        message("process '%s' successfully loaded plugin '%s' at path '%s' into new namespace '%p' in %f seconds",
                _process_getName(proc), _process_getPluginName(proc), _process_getPluginPath(proc),
                proc->plugin.handle, secondsElapsedDuringLoad);
    } else {
        critical("dlmopen() failed to load plugin '%s': %s", proc->plugin.path->str, errorMessage);
        error("unable to load private plug-in '%s'", proc->plugin.path->str);
    }
    /* clear dlerror status string */
    dlerror();

    /* get the LMID so we can load it in the same namespace as the plugin */
    Lmid_t lmid = 0;
    int result = dlinfo(proc->plugin.handle, RTLD_DI_LMID, &lmid);

    const gchar* errorMessage2 = dlerror();

    if(result == 0) {
        debug("found LMID %lu for handle %p", (long unsigned int)lmid, proc->plugin.handle);
        proc->lmid = lmid;
    } else {
        critical("dlinfo() failed when querying for LMID: %s", errorMessage2);
        error("unable to load preload library '%s'", proc->plugin.preloadPath->str);
    }
    /* do we also need to load in a preload library for this plugin? */
    if(proc->plugin.preloadPath) {
        /* reset the timer so we can time loading the preload lib */
        g_timer_start(loadTimer);

        /* dlmopen may result in plugin constructors getting called, so make sure
         * we make that call from the plugin context. */
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);

        /* clear dlerror status string */
        dlerror();

        /* now we have the correct lmid, lets load our preload library into it */
        dlmopen(lmid, proc->plugin.preloadPath->str, RTLD_LAZY|RTLD_GLOBAL|RTLD_INTERPOSE);

        const gchar* errorMessage3 = dlerror();

        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);

        /* check the load timer */
        secondsElapsedDuringLoad = g_timer_elapsed(loadTimer, NULL);

        if(!errorMessage3) {
            message("process '%s' successfully loaded preload '%s' at path '%s' into existing namespace '%p' in %f seconds",
                    _process_getName(proc), proc->plugin.preloadName->str, proc->plugin.preloadPath->str,
                    proc->plugin.handle, secondsElapsedDuringLoad);
        } else {
            critical("dlinfo() failed to load preload '%s': %s", proc->plugin.path->str, errorMessage3);
            error("unable to load preload library '%s'", proc->plugin.preloadPath->str);
        }
    }

    g_timer_destroy(loadTimer);

    /* the remaining dlsym lookups should not cause code inside the plugin to get
     * executed, so we should be able to do them from the shadow context. */

    /* clear dlerror status string */
    dlerror();

    /* make sure it has the required init function */
    gpointer symbol = NULL;

    symbol = dlsym(proc->plugin.handle, _process_getPluginStartSymbol(proc) ?
                   _process_getPluginStartSymbol(proc) : PLUGIN_DEFAULT_SYMBOL);
    if(symbol) {
        proc->plugin.main = symbol;
        message("found '%s' at %p", _process_getPluginStartSymbol(proc), symbol);
    } else {
        const gchar* errorMessage = dlerror();
        critical("dlsym() failed: %s", errorMessage);
        error("unable to find the required function symbol '%s' in plug-in '%s'",
                _process_getPluginStartSymbol(proc) ? 
                _process_getPluginStartSymbol(proc) : PLUGIN_DEFAULT_SYMBOL,
                _process_getPluginPath(proc));
    }

    /* search for the location of errno and save it in the plugin state */
    _process_updateErrnoLocation(proc);

    /* clear dlerror status string */
    dlerror();

    symbol = NULL;
    symbol = dlsym(proc->plugin.handle, PLUGIN_POSTLOAD_SYMBOL);
    if(symbol) {
        proc->plugin.postLibraryLoad = symbol;
        message("found '%s' at %p", PLUGIN_POSTLOAD_SYMBOL, symbol);
    }

    symbol = NULL;
    symbol = dlsym(proc->plugin.handle, PLUGIN_PREUNLOAD_SYMBOL);
    if(symbol) {
        proc->plugin.preLibraryUnload = symbol;
        message("found '%s' at %p", PLUGIN_PREUNLOAD_SYMBOL, symbol);
    }

    symbol = NULL;
    symbol = dlsym(proc->plugin.handle, PLUGIN_PREENTER_SYMBOL);
    if(symbol) {
        proc->plugin.preProcessEnter = symbol;
        message("found '%s' at %p", PLUGIN_PREENTER_SYMBOL, symbol);
    }

    symbol = NULL;
    symbol = dlsym(proc->plugin.handle, PLUGIN_POSTEXIT_SYMBOL);
    if(symbol) {
        proc->plugin.postProcessExit = symbol;
        message("found '%s' at %p", PLUGIN_POSTEXIT_SYMBOL, symbol);
    }

    /* install a signal handler for errors that happen inside of this namespace */
    symbol = NULL;
    symbol = dlsym(proc->plugin.handle, "sigaction");
    if(symbol) {
        /* setup and install the handler for deadly signals */
        proc->plugin.sigaction = symbol;

        struct sigaction action;
        action.sa_handler = _process_pluginSignalHandler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;

        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
        proc->plugin.sigaction(SIGSEGV, &action, NULL);
        proc->plugin.sigaction(SIGFPE, &action, NULL);
        proc->plugin.sigaction(SIGABRT, &action, NULL);
        proc->plugin.sigaction(SIGILL, &action, NULL);
        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
    }

    /* call one of the postload callbacks if needed */
    if(proc->plugin.postLibraryLoad != NULL) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
        proc->plugin.postLibraryLoad(proc->plugin.handle);
        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
    }
}

Process* process_new(gpointer host, guint processID,
        SimulationTime startTime, SimulationTime stopTime, const gchar* pluginName,
        const gchar* pluginPath, const gchar* pluginSymbol, const gchar* preloadName,
        const gchar* preloadPath, gchar* arguments) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    proc->host = (Host*)host;
    if(proc->host) {
        host_ref(proc->host);
    }

    proc->processID = processID;

    utility_assert(pluginPath);
    utility_assert(pluginName);
    proc->plugin.name = g_string_new(pluginName);
    proc->plugin.path = g_string_new(pluginPath);
    if(pluginSymbol) {
        proc->plugin.startSymbol = g_string_new(pluginSymbol);
    }
    if(preloadName && preloadPath) {
        proc->plugin.preloadName = g_string_new(preloadName);
        proc->plugin.preloadPath = g_string_new(preloadPath);
    }

    proc->processName = g_string_new(NULL);
    g_string_printf(proc->processName, "%s.%s.%u",
            host_getName(proc->host), _process_getPluginName(proc), proc->processID);

    proc->startTime = startTime;
    proc->stopTime = stopTime;
    if(arguments && (g_ascii_strncasecmp(arguments, "\0", (gsize) 1) != 0)) {
        proc->arguments = g_string_new(arguments);
    }

    proc->cpuDelayTimer = g_timer_new();
    proc->referenceCount = 1;
    proc->activeContext = PCTX_SHADOW;

    proc->programAuxThreads = g_hash_table_new(g_direct_hash, g_direct_equal);

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_NEW);

    return proc;
}

static void _process_logCachedWarnings(Process* proc) {
    if(proc->cachedWarningMessages) {
        gchar* msgStr = NULL;
        while((msgStr = g_queue_pop_head(proc->cachedWarningMessages)) != NULL) {
            warning(msgStr);
            g_free(msgStr);
        }
    }
}

static void _process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    /* stop and free plugin memory if we are still running */
    if(process_isRunning(proc)) {
        process_stop(proc);
    }

    if(proc->arguments) {
        g_string_free(proc->arguments, TRUE);
    }

    if(proc->atExitFunctions) {
        g_queue_free_full(proc->atExitFunctions, g_free);
    }

    if(proc->stdoutFile) {
        fclose(proc->stdoutFile);
        proc->stdoutFile = NULL;
    }
    if(proc->stderrFile) {
        fclose(proc->stderrFile);
        proc->stderrFile = NULL;
    }

    if(proc->cachedWarningMessages) {
        _process_logCachedWarnings(proc);
        g_queue_free(proc->cachedWarningMessages);
    }
    if(proc->plugin.path) {
        g_string_free(proc->plugin.path, TRUE);
    }
    if(proc->plugin.name) {
        g_string_free(proc->plugin.name, TRUE);
    }
    if(proc->plugin.startSymbol) {
        g_string_free(proc->plugin.startSymbol, TRUE);
    }
    if(proc->plugin.preloadPath) {
        g_string_free(proc->plugin.preloadPath, TRUE);
    }
    if(proc->plugin.preloadName) {
        g_string_free(proc->plugin.preloadName, TRUE);
    }
    if(proc->processName) {
        g_string_free(proc->processName, TRUE);
    }

    g_timer_destroy(proc->cpuDelayTimer);

    if(proc->host) {
        host_unref(proc->host);
    }

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_FREE);

    MAGIC_CLEAR(proc);
    g_free(proc);
}

static FILE* _process_openFile(Process* proc, const gchar* prefix) {
    const gchar* hostDataPath = host_getDataPath(proc->host);
    GString* fileNameString = g_string_new(NULL);
    g_string_printf(fileNameString, "%s-%s.log", prefix, _process_getName(proc));
    gchar* pathStr = g_build_filename(hostDataPath, fileNameString->str, NULL);
    FILE* f = g_fopen(pathStr, "a");
    g_string_free(fileNameString, TRUE);
    if(!f) {
        /* if we log as normal, glib will freak out about recursion if the plugin was trying to log with glib */
        if(!proc->cachedWarningMessages) {
            proc->cachedWarningMessages = g_queue_new();
        }
        GString* stringBuffer = g_string_new(NULL);
        g_string_printf(stringBuffer, "process '%s': unable to open file '%s', error was: %s",
                _process_getName(proc), pathStr, g_strerror(errno));
        g_queue_push_tail(proc->cachedWarningMessages, g_string_free(stringBuffer, FALSE));

//        warning("process '%s-%u': unable to open file '%s', error was: %s",
//                _process_getPluginName(proc), proc->processID, pathStr, g_strerror(errno));
    }
    g_free(pathStr);
    return f;
}

static FILE* _process_getIOFile(Process* proc, gint fd){
    MAGIC_ASSERT(proc);
    utility_assert(fd == STDOUT_FILENO || fd == STDERR_FILENO);

    if(fd == STDOUT_FILENO) {
        if(!proc->stdoutFile) {
            proc->stdoutFile = _process_openFile(proc, "stdout");
            if(!proc->stdoutFile) {
                /* if we log as normal, glib will freak out about recursion if the plugin was trying to log with glib */
                if(!proc->cachedWarningMessages) {
                    proc->cachedWarningMessages = g_queue_new();
                }
                GString* stringBuffer = g_string_new(NULL);
                g_string_printf(stringBuffer, "process '%s': unable to open file for process output, dumping to tty stdout",
                    _process_getName(proc));
                g_queue_push_tail(proc->cachedWarningMessages, g_string_free(stringBuffer, FALSE));

                /* now set shadows stdout */
                proc->stdoutFile = stdout;
            }
        }
        return proc->stdoutFile;
    } else {
        if(!proc->stderrFile) {
            proc->stderrFile = _process_openFile(proc, "stderr");
            if(!proc->stderrFile) {
                /* if we log as normal, glib will freak out about recursion if the plugin was trying to log with glib */
                if(!proc->cachedWarningMessages) {
                    proc->cachedWarningMessages = g_queue_new();
                }
                GString* stringBuffer = g_string_new(NULL);
                g_string_printf(stringBuffer, "process '%s': unable to open file for process errors, dumping to tty stderr",
                        _process_getName(proc));
                g_queue_push_tail(proc->cachedWarningMessages, g_string_free(stringBuffer, FALSE));

                /* now set shadows stderr */
                proc->stderrFile = stderr;
            }
        }
        return proc->stderrFile;
    }
}

static void _process_handleTimerResult(Process* proc, gdouble elapsedTimeSec) {
    SimulationTime delay = (SimulationTime) (elapsedTimeSec * SIMTIME_ONE_SECOND);
    Host* currentHost = worker_getActiveHost();
    cpu_addDelay(host_getCPU(currentHost), delay);
    tracker_addProcessingTime(host_getTracker(currentHost), delay);
}

static gint _process_getArguments(Process* proc, gchar** argvOut[]) {
    gchar* threadBuffer;

    GQueue *arguments = g_queue_new();

    /* first argument is the name of the program */
    const gchar* pluginName = _process_getPluginName(proc);
    g_queue_push_tail(arguments, g_strdup(pluginName));

    /* parse the full argument string into separate strings */
    if(proc->arguments && proc->arguments->len > 0 && g_ascii_strncasecmp(proc->arguments->str, "\0", (gsize) 1) != 0) {
        gchar* argumentString = g_strdup(proc->arguments->str);
        gchar* token = strtok_r(argumentString, " ", &threadBuffer);
        while(token != NULL) {
            gchar* argument = g_strdup((const gchar*) token);
            g_queue_push_tail(arguments, argument);
            token = strtok_r(NULL, " ", &threadBuffer);
        }
        g_free(argumentString);
    }

    /* setup for creating new plug-in, i.e. format into argc and argv */
    gint argc = g_queue_get_length(arguments);
    /* a pointer to an array that holds pointers */
    gchar** argv = g_new0(gchar*, argc);

    for(gint i = 0; i < argc; i++) {
        argv[i] = g_queue_pop_head(arguments);
    }

    /* cleanup */
    g_queue_free(arguments);

    /* transfer to the caller - they must free argv and each element of it */
    *argvOut = argv;
    return argc;
}

static void _process_executeAtFork(ProcessAtForkCallbackData* data) {
    if(data) {
        Process* proc = data->proc;
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        /* sanity checks */
        MAGIC_ASSERT(proc);
        utility_assert(process_isRunning(proc));
        utility_assert(worker_getActiveProcess() == proc);

        if(data->prepare || data->parent || data->child) {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);

            if(data->prepare)
                data->prepare();
            else if(data->parent)
                data->parent();
            else if(data->child)
                data->child();

            _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
        }

        int count = data->proc->referenceCount;
        process_unref(data->proc);
        g_free(data);
        if(count > 1) {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        }
    }
}

static void* _process_executeChild(ProcessChildData* data) {
    Process* proc = data->proc;

    /* we just came from pth_spawn - the first thing we should do is update our context
     * back to shadow to make sure any potential shadow sys calls get handled correctly */
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

    /* sanity checks */
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));
    utility_assert(worker_getActiveProcess() == proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    /* now we are entering the plugin program via a pth thread */
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);

    /* call the thread start routine, pth will handle blocking as the thread runs */
    gpointer ret = data->run(data->arg);

    /* this thread has completed */
    _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);

    /* no need to call stop */
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    /* when we return, pth will call the exit functions queued for the main thread */
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    /* unref for the data object */
    process_unref(proc);

    /* free it */
    g_free(data);

    return ret;
}

static void _process_executeCleanup(Process* proc) {
    /* we just came from pth_spawn - the first thing we should do is update our context
     * back to shadow to make sure any potential shadow sys calls get handled correctly */
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

    /* sanity checks */
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));
    utility_assert(worker_getActiveProcess() == proc);

    guint numThreads = g_hash_table_size(proc->programAuxThreads);
    guint numExitFuncs = proc->atExitFunctions ? g_queue_get_length(proc->atExitFunctions) : 0;
    message("cleaning up process '%s': aborting %u auxiliary threads and calling %u atexit functions",
            _process_getName(proc), numThreads, numExitFuncs);

    /* closing the main thread causes all other threads to get terminated */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, proc->programAuxThreads);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        pth_t auxThread = key;
        if(auxThread) {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            gint success = pth_abort(auxThread);
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        }
    }
    g_hash_table_remove_all(proc->programAuxThreads);

    /* calling the process atexit funcs. these shouldnt use any thread data that got deleted above */
    while(proc->atExitFunctions && g_queue_get_length(proc->atExitFunctions) > 0) {
        ProcessExitCallbackData* atexitData = g_queue_pop_head(proc->atExitFunctions);

        /* time the program execution */
        g_timer_start(proc->cpuDelayTimer);

        /* call the plugin's cleanup callback */
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
        if(atexitData->passArgument) {
            ((PluginExitCallbackArgumentsFunc)atexitData->callback)(0, atexitData->argument);
        } else {
            ((PluginExitCallbackFunc)atexitData->callback)();
        }
        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);

        /* no need to call stop */
        gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
        _process_handleTimerResult(proc, elapsed);

        g_free(atexitData);
    }

    /* flush program output */
    if(proc->stdoutFile) {
        fflush(proc->stdoutFile);
        fclose(proc->stdoutFile);
        proc->stdoutFile = NULL;
    }
    if(proc->stderrFile) {
        fflush(proc->stderrFile);
        fclose(proc->stderrFile);
        proc->stderrFile = NULL;
    }

    if(proc->argv) {
        /* free the arguments */
        for(gint i = 0; i < proc->argc; i++) {
            g_free(proc->argv[i]);
        }
        g_free(proc->argv);
        proc->argv = NULL;
    }

    /* the main thread is done and will be joined by pth */
    proc->programMainThread = NULL;

    /* unref for the main func */
    process_unref(proc);

    /* unref for the cleanup func */
    int count = proc->referenceCount;
    process_unref(proc);

    /* we return to pth control */
    if(count > 1) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
    }
}

static void _process_logReturnCode(Process* proc, gint code) {
    if(!proc->returnCodeLogged) {
        GString* mainResultString = g_string_new(NULL);
        g_string_printf(mainResultString, "main %s code '%i' for process '%s'",
                ((code==0) ? "success" : "error"),
                code, _process_getName(proc));

        if(code == 0) {
            message("%s", mainResultString->str);
        } else {
            warning("%s", mainResultString->str);
            worker_incrementPluginError();
        }

        g_string_free(mainResultString, TRUE);

        proc->returnCodeLogged = TRUE;
    }
}

static void* _process_executeMain(Process* proc) {
    /* we just came from pth_spawn - the first thing we should do is update our context
     * back to shadow to make sure any potential shadow sys calls get handled correctly */
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

    /* sanity checks */
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));
    utility_assert(worker_getActiveProcess() == proc);

    /* ref for the cleanup func below */
    process_ref(proc);

    /* let's go back to pth momentarily and push the cleanup function for the main thread */
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
    pth_cleanup_push((PthCleanupFunc)(&_process_executeCleanup), proc);
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

    /* get arguments from the program we will run */
    proc->argc = _process_getArguments(proc, &proc->argv);

    message("calling main() for process '%s'", _process_getName(proc));

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    /* now we are entering the plugin program via a pth thread */
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);

    /* call the program's main function, pth will handle blocking as the program runs */
    utility_assert(proc->plugin.isExecuting);
    utility_assert(proc->plugin.main);
    proc->returnCode = proc->plugin.main(proc->argc, proc->argv);

    /* the program's main function has returned or exited, this process has completed */
    _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);

    /* commit output to file asap */
    if(proc->stdoutFile) {
        fflush(proc->stdoutFile);
    }
    if(proc->stderrFile) {
        fflush(proc->stderrFile);
    }

    /* no need to call stop */
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    _process_logReturnCode(proc, proc->returnCode);

    /* when we return, pth will call the exit functions queued for the main thread */
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
    return NULL;
}

gboolean process_addAtExitCallback(Process* proc, gpointer userCallback, gpointer userArgument,
        gboolean shouldPassArgument) {
    MAGIC_ASSERT(proc);
    if(!process_isRunning(proc)) {
        return FALSE;
    }

    if(userCallback) {
        ProcessExitCallbackData* exitCallback = g_new0(ProcessExitCallbackData, 1);
        exitCallback->callback = userCallback;
        exitCallback->argument = userArgument;
        exitCallback->passArgument = shouldPassArgument;

        if(!proc->atExitFunctions) {
            proc->atExitFunctions = g_queue_new();
        }

        g_queue_push_head(proc->atExitFunctions, exitCallback);
    }

    return TRUE;
}

static void _process_start(Process* proc) {
    MAGIC_ASSERT(proc);

    /* dont do anything if we are already running */
    if(process_isRunning(proc)) {
        return;
    }

    message("starting process '%s'", _process_getName(proc));

    /* start a timer for initialization tasks */
    GTimer* initTimer = g_timer_new();

    /* create the thread names while still in shadow context, format is host.process.<id> */
    GString* shadowThreadNameBuf = g_string_new(NULL);
    g_string_printf(shadowThreadNameBuf, "%s.shadow", _process_getName(proc));
    GString* programMainThreadNameBuf = g_string_new(NULL);
    g_string_printf(programMainThreadNameBuf, "%s.main", _process_getName(proc));

    utility_assert(g_hash_table_size(proc->programAuxThreads) == 0);

    /* ref for the main func (spawn) below */
    process_ref(proc);

    /* now we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);
    proc->plugin.isExecuting = TRUE;
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    /* create a new global context for this process, 0 means it should never block */
    proc->tstate = pth_gctx_new(0);

    /* we are in pth land, load in the pth state for this process */
    pth_gctx_t prevPthGlobalContext = pth_gctx_get();
    pth_gctx_set(proc->tstate);

    /* pth_gctx_new implicitly created a 'main' thread, which shadow now runs in */
    proc->shadowThread = pth_self();

    /* it also created a special epollfd which we will use to continue the pth scheduler */
    proc->epollfd = pth_gctx_get_main_epollfd(proc->tstate);

    /* set some defaults for out special shadow thread: not joinable, and set the
     * min (worst) priority so that all other threads will run before coming back to shadow
     * (the main thread is special in pth, and has a stack size of 0 internally ) */
    pth_attr_t shadowThreadAttr = pth_attr_of(proc->shadowThread);
    pth_attr_set(shadowThreadAttr, PTH_ATTR_NAME, shadowThreadNameBuf->str);
    pth_attr_set(shadowThreadAttr, PTH_ATTR_JOINABLE, FALSE);
    pth_attr_set(shadowThreadAttr, PTH_ATTR_PRIO, PTH_PRIO_MIN);
    pth_attr_destroy(shadowThreadAttr);

    /* spawn the program main thread: joinable by default, bigger stack */
    pth_attr_t programMainThreadAttr = pth_attr_new();
    pth_attr_set(programMainThreadAttr, PTH_ATTR_NAME, programMainThreadNameBuf->str);
    pth_attr_set(programMainThreadAttr, PTH_ATTR_STACK_SIZE, PROC_PTH_STACK_SIZE);
    proc->programMainThread = pth_spawn(programMainThreadAttr, (PthSpawnFunc)_process_executeMain, proc);
    pth_attr_destroy(programMainThreadAttr);

    /* now that our pth state is set up, load the plugin */
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    gdouble secondsToInitPth = g_timer_elapsed(initTimer, NULL);
    g_timer_start(initTimer);
    _process_loadPlugin(proc);
    gdouble secondsToInitPlugin = g_timer_elapsed(initTimer, NULL);
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    utility_assert(proc->plugin.isExecuting);
    g_timer_start(initTimer);
    if(proc->plugin.preProcessEnter != NULL) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
        proc->plugin.preProcessEnter(proc->plugin.handle);
        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
    }
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    /* now give the main program thread a chance to run */
    pth_yield(proc->programMainThread);

    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    utility_assert(proc->plugin.isExecuting);
    if(proc->plugin.postProcessExit != NULL) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
        proc->plugin.postProcessExit(proc->plugin.handle);
        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
    }
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
    gdouble secondsUntilMainBlocked = g_timer_elapsed(initTimer, NULL);

    /* total number of alive pth threads this scheduler has */
    gint nThreads = pth_ctrl(PTH_CTRL_GETTHREADS_NEW|PTH_CTRL_GETTHREADS_READY|\
            PTH_CTRL_GETTHREADS_RUNNING|PTH_CTRL_GETTHREADS_WAITING|PTH_CTRL_GETTHREADS_SUSPENDED);

    /* revert pth global context */
    pth_gctx_set(prevPthGlobalContext);

    /* the main function finished or blocked somewhere and we are back in shadow land */
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    proc->plugin.isExecuting = FALSE;
    worker_setActiveProcess(NULL);

    message("process '%s' initialized the pth threading system in %f seconds, "
            "initialized the plugin namespace in %f seconds, "
            "and ran the pth main thread until it blocked in %f seconds",
            _process_getName(proc), secondsToInitPth, secondsToInitPlugin, secondsUntilMainBlocked);

    /* the main thread wont exist if it exited immediately before returning control to shadow */
    if(proc->programMainThread) {
        message("process '%s' has set up the main pth thread '%s' and %s running",
                _process_getName(proc), programMainThreadNameBuf->str,
                process_isRunning(proc) ? "is" : "is not");
    } else {
        _process_logReturnCode(proc, proc->returnCode);

        utility_assert(nThreads == 1);

        proc->tstate = NULL;

        /* free our copy of plug-in resources, and other application state */
        //_process_unloadPlugin(proc); XXX TODO this should be done once elf-loader supports unloading libs
        utility_assert(!process_isRunning(proc));

        message("process '%s' has completed or is otherwise no longer running", _process_getName(proc));
    }

    g_timer_destroy(initTimer);

    if(proc->stdoutFile) {
        fflush(proc->stdoutFile);
    }
    if(proc->stderrFile) {
        fflush(proc->stderrFile);
    }
    if(proc->cachedWarningMessages) {
        _process_logCachedWarnings(proc);
    }

    /* cleanup */
    g_string_free(shadowThreadNameBuf, TRUE);
    g_string_free(programMainThreadNameBuf, TRUE);
}

void process_continue(Process* proc) {
    MAGIC_ASSERT(proc);

    /* if we are not running, no need to notify anyone */
    if(!process_isRunning(proc)) {
        return;
    }

    info("switching to rpth to continue the threads of process '%s'", _process_getName(proc));

    /* there is some i/o or event available, let pth handle it
     * we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);
    proc->plugin.isExecuting = TRUE;
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    /* we are in pth land, load in the pth state for this process */
    pth_gctx_t prevPthGlobalContext = pth_gctx_get();
    pth_gctx_set(proc->tstate);

    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    utility_assert(proc->plugin.isExecuting);
    if(proc->plugin.preProcessEnter != NULL) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
        proc->plugin.preProcessEnter(proc->plugin.handle);
        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
    }
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    /* make sure pth scheduler updates, and process all program threads until they block */
    do {
        pth_yield(NULL);
    } while(pth_ctrl(PTH_CTRL_GETTHREADS_READY | PTH_CTRL_GETTHREADS_NEW));

    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    utility_assert(proc->plugin.isExecuting);
    if(proc->plugin.postProcessExit != NULL) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PLUGIN);
        proc->plugin.postProcessExit(proc->plugin.handle);
        _process_changeContext(proc, PCTX_PLUGIN, PCTX_SHADOW);
    }
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    /* total number of alive pth threads this scheduler has */
    gint nThreads = pth_ctrl(PTH_CTRL_GETTHREADS_NEW|PTH_CTRL_GETTHREADS_READY|\
            PTH_CTRL_GETTHREADS_RUNNING|PTH_CTRL_GETTHREADS_WAITING|PTH_CTRL_GETTHREADS_SUSPENDED);

    /* if the main thread closed, this process is done */
    if(!proc->programMainThread) {
        /* now we are done with all pth state */
//        pth_gctx_free(proc->tstate); // XXX FIXME this causes other nodes' processes to end also:(
        proc->tstate = NULL;
    }

    /* revert pth global context */
    pth_gctx_set(prevPthGlobalContext);

    /* the pth threads finished or blocked somewhere and we are back in shadow land */
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    proc->plugin.isExecuting = FALSE;
    worker_setActiveProcess(NULL);

    if(proc->cachedWarningMessages) {
        _process_logCachedWarnings(proc);
    }

    if(proc->programMainThread) {
        info("process '%s' is running, but threads are blocked waiting for events", _process_getName(proc));
    } else {
        /* pth should have had no remaining alive threads except the one shadow was running in */
        utility_assert(nThreads == 1);

        /* free our copy of plug-in resources, and other application state */
        //_process_unloadPlugin(proc); XXX TODO this should be done once elf-loader supports unloading libs
        utility_assert(!process_isRunning(proc));

        info("process '%s' has completed or is otherwise no longer running", _process_getName(proc));
    }
}

gboolean process_wantsNotify(Process* proc, gint epollfd) {
    MAGIC_ASSERT(proc);
    if(process_isRunning(proc) && epollfd == proc->epollfd) {
        return TRUE;
    } else {
        return FALSE;
    }
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    /* we only have state if we are running */
    if(!process_isRunning(proc)) {
        return;
    }

    message("terminating main thread of process '%s'", _process_getName(proc));

    worker_setActiveProcess(proc);
    proc->plugin.isExecuting = TRUE;
    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

    /* we are in pth land, load in the pth state for this process */
    pth_gctx_t prevPthGlobalContext = pth_gctx_get();
    pth_gctx_set(proc->tstate);

    /* this should stop the thread and call the main thread cleanup function */
    if(proc->programMainThread != NULL) {
        pth_abort(proc->programMainThread);
        proc->programMainThread = NULL;
    }

    /* now we are done with all pth state */
    pth_gctx_free(proc->tstate);
    proc->tstate = NULL;

    /* revert pth global context */
    pth_gctx_set(prevPthGlobalContext);

    /* the pth threads finished or blocked somewhere and we are back in shadow land */
    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    proc->plugin.isExecuting = FALSE;
    worker_setActiveProcess(NULL);

    /* free our copy of plug-in resources, and other application state */
    //_process_unloadPlugin(proc); XXX TODO this should be done once elf-loader supports unloading libs
}

static void _process_runStartTask(Process* proc, gpointer nothing) {
    _process_start(proc);
}

static void _process_runStopTask(Process* proc, gpointer nothing) {
    process_stop(proc);
}

void process_schedule(Process* proc, gpointer nothing) {
    MAGIC_ASSERT(proc);

    SimulationTime now = worker_getCurrentTime();

    if(proc->stopTime == 0 || proc->startTime < proc->stopTime) {
        SimulationTime startDelay = proc->startTime <= now ? 1 : proc->startTime - now;
        process_ref(proc);
        Task* startProcessTask = task_new((TaskCallbackFunc)_process_runStartTask,
                proc, NULL, (TaskObjectFreeFunc)process_unref, NULL);
        worker_scheduleTask(startProcessTask, startDelay);
        task_unref(startProcessTask);
    }

    if(proc->stopTime > 0 && proc->stopTime > proc->startTime) {
        SimulationTime stopDelay = proc->stopTime <= now ? 1 : proc->stopTime - now;
        process_ref(proc);
        Task* stopProcessTask = task_new((TaskCallbackFunc)_process_runStopTask,
                proc, NULL, (TaskObjectFreeFunc)process_unref, NULL);
        worker_scheduleTask(stopProcessTask, stopDelay);
        task_unref(stopProcessTask);
    }
}

void process_ref(Process* proc) {
    MAGIC_ASSERT(proc);
    (proc->referenceCount)++;
}

void process_unref(Process* proc) {
    MAGIC_ASSERT(proc);
    (proc->referenceCount)--;
    utility_assert(proc->referenceCount >= 0);
    if(proc->referenceCount == 0) {
        _process_free(proc);
    }
}

gboolean process_isRunning(Process* proc) {
    MAGIC_ASSERT(proc);
    return (proc->tstate != NULL) ? TRUE : FALSE;
}

gboolean process_shouldEmulate(Process* proc) {
    return ((!proc) || (proc->activeContext == PCTX_SHADOW)) ? FALSE : TRUE;
}

void process_migrate(Process* proc, gpointer threads) {
    MAGIC_ASSERT(proc);
    struct ProcessMigrateArgs* ts = threads;
    if (!proc->lmid) {
        /* plugin hasn't been loaded into a namespace yet; nothing to do */
        info("can't migrate process before namespace is loaded");
        return;
    }
    if (!ts || !ts->t1 || !ts->t2) {
        /* can't swap to/from NULL threads */
        warning("can't migrate process to/from NULL threads");
        return;
    }
    int ret = dl_lmid_swap_tls (proc->lmid, ts->t1, ts->t2);
    if (ret != 0) {
        error("could not find lmid %p", proc->lmid);
    }
    /* now that we migrated the TLS, the errno location for this proc is no longer valid.
     * set the flag so that the next thread executing this process does a new lookup before
     * trying to set errno again. */
    proc->plugin.errnoGetLocationIsStale = TRUE;
}

/*****************************************************************
 * Begin virtual process emulation of pthread and syscalls.
 * These functions have been interposed by the preload library
 * to hijack control over the flow of execution.
 *****************************************************************/

/* static helper functions */

static gint _process_emu_addressHelper(Process* proc, gint fd, const struct sockaddr* addr, socklen_t* len,
        enum _SystemCallType type) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint result = 0;

    /* check if this is a virtual socket */
    if(!host_isShadowDescriptor(proc->host, fd)){
        warning("intercepted a non-virtual descriptor");
        result = EBADF;
    } else if(addr == NULL) { /* check for proper addr */
        result = EFAULT;
    } else if(len == NULL) {
        result = EINVAL;
    }

    if(result == 0) {
        /* direct to proc->host for further checks */
        switch(type) {
            case SCT_BIND: {
                result = host_bindToInterface(proc->host, fd, addr);
                break;
            }

            case SCT_CONNECT: {
                result = host_connectToPeer(proc->host, fd, addr);
                break;
            }

            case SCT_GETPEERNAME:
            case SCT_GETSOCKNAME: {
                result = type == SCT_GETPEERNAME ?
                        host_getPeerName(proc->host, fd, addr, len) :
                        host_getSocketName(proc->host, fd, addr, len);
                break;
            }

            default: {
                result = EINVAL;
                error("unrecognized system call type");
                break;
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    /* check if there was an error */
    if(result != 0) {
        _process_setErrno(proc, result);
        return -1;
    }

    return 0;
}

static gssize _process_emu_sendHelper(Process* proc, gint fd, gconstpointer buf, gsize n, gint flags,
        const struct sockaddr* addr, socklen_t len) {
    /* this function MUST be called after switching in shadow context */
    utility_assert(proc->activeContext == PCTX_SHADOW);

    /* TODO flags are ignored */
    /* make sure this is a socket */
    if(!host_isShadowDescriptor(proc->host, fd)){
        _process_setErrno(proc, EBADF);
        return -1;
    }

    in_addr_t ip = 0;
    in_port_t port = 0;

    /* check if they specified an address to send to */
    if(addr != NULL && len >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in* si = (struct sockaddr_in*) addr;
        ip = si->sin_addr.s_addr;
        port = si->sin_port;
    }

    gsize bytes = 0;
    gint result = host_sendUserData(proc->host, fd, buf, n, ip, port, &bytes);

    if(result != 0) {
        _process_setErrno(proc, result);
        return -1;
    }
    return (gssize) bytes;
}

static gssize _process_emu_recvHelper(Process* proc, gint fd, gpointer buf, size_t n, gint flags,
        struct sockaddr* addr, socklen_t* len) {
    /* this function MUST be called after switching in shadow context */
    utility_assert(proc->activeContext == PCTX_SHADOW);

    /* TODO flags are ignored */
    /* make sure this is a socket */
    if(!host_isShadowDescriptor(proc->host, fd)){
        _process_setErrno(proc, EBADF);
        return -1;
    }

    in_addr_t ip = 0;
    in_port_t port = 0;

    gsize bytes = 0;
    gint result = host_receiveUserData(proc->host, fd, buf, n, &ip, &port, &bytes);

    if(result != 0) {
        _process_setErrno(proc, result);
        return -1;
    }

    /* check if they wanted to know where we got the data from */
    if(addr != NULL && len != NULL && *len >= sizeof(struct sockaddr_in)) {
        struct sockaddr_in* si = (struct sockaddr_in*) addr;
        si->sin_addr.s_addr = ip;
        si->sin_port = port;
        si->sin_family = AF_INET;
        *len = sizeof(struct sockaddr_in);
    }

    return (gssize) bytes;
}

static gint _process_emu_fcntlHelper(Process* proc, int fd, int cmd, void* argp) {
    /* check if this is a socket */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(!host_isShadowDescriptor(proc->host, fd)){
        gint ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd >= 0) {
            ret = fcntl(osfd, cmd, argp);
            if(ret < 0) {
                _process_setErrno(proc, errno);
            }
        } else {
            _process_setErrno(proc, EBADF);
            ret = -1;
        }
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return ret;
    }

    /* normally, the type of farg depends on the cmd */
    Descriptor* descriptor = host_lookupDescriptor(proc->host, fd);

    gint result = 0;
    if(descriptor) {
        if (cmd == F_GETFL) {
            result = descriptor_getFlags(descriptor);
        } else if (cmd == F_SETFL) {
            gint flags = GPOINTER_TO_INT(argp);
            descriptor_setFlags(descriptor, flags);
        }
    } else {
        _process_setErrno(proc, EBADF);
        result = -1;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

static gint _process_emu_ioctlHelper(Process* proc, int fd, unsigned long int request, void* argp) {
    /* check if this is a socket */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(!host_isShadowDescriptor(proc->host, fd)){
        gint ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd >= 0) {
            ret = ioctl(osfd, request, argp);
            if(ret < 0) {
                _process_setErrno(proc, errno);
            }
        } else {
            _process_setErrno(proc, EBADF);
            ret = -1;
        }
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return ret;
    }

    gint result = 0;

    /* normally, the type of farg depends on the request */
    Descriptor* descriptor = host_lookupDescriptor(proc->host, fd);

    if(descriptor) {
        DescriptorType t = descriptor_getType(descriptor);
        if(t == DT_TCPSOCKET) {
            TCP* tcpSocket = (TCP*) descriptor;
            if(request == SIOCINQ || request == FIONREAD) {
                gsize bufferLength = tcp_getInputBufferLength(tcpSocket);
                gint* lengthOut = (gint*)argp;
                *lengthOut = (gint)bufferLength;
            } else if (request == SIOCOUTQ || request == TIOCOUTQ) {
                gsize bufferLength = tcp_getOutputBufferLength(tcpSocket);
                gint* lengthOut = (gint*)argp;
                *lengthOut = (gint)bufferLength;
            } else {
                result = ENOTTY;
            }
        } else if(t == DT_UDPSOCKET) {
            Socket* socket = (Socket*) descriptor;
            if(request == SIOCINQ || request == FIONREAD) {
                gsize bufferLength = socket_getInputBufferLength(socket);
                gint* lengthOut = (gint*)argp;
                *lengthOut = (gint)bufferLength;
            } else if (request == SIOCOUTQ || request == TIOCOUTQ) {
                gsize bufferLength = socket_getOutputBufferLength(socket);
                gint* lengthOut = (gint*)argp;
                *lengthOut = (gint)bufferLength;
            } else {
                result = ENOTTY;
            }
        } else {
            result = ENOTTY;
        }
    } else {
        result = EBADF;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

static int _process_emu_selectHelper(Process* proc, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout) {
    /* this function MUST be called after switching in shadow context */
    utility_assert(proc->activeContext == PCTX_SHADOW);
    gint ret = 0;

    if (nfds < 0 || nfds > FD_SETSIZE) {
        _process_setErrno(proc, EINVAL);
        ret = -1;
    } else if(nfds == 0 && readfds == NULL && writefds == NULL && exceptfds == NULL && timeout != NULL) {
        /* only wait for the timeout, no file descriptor events */
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        pth_nanosleep(timeout, NULL);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        fd_set* tmpReadFDs = NULL;
        if(readfds) {
            tmpReadFDs = g_new0(fd_set, 1);
            FD_ZERO(tmpReadFDs);
            g_memmove(tmpReadFDs, readfds, sizeof(fd_set));
        }
        fd_set* tmpWriteFDs = NULL;
        if(writefds) {
            tmpWriteFDs = g_new0(fd_set, 1);
            FD_ZERO(tmpWriteFDs);
            g_memmove(tmpWriteFDs, writefds, sizeof(fd_set));
        }
        fd_set* tmpExceptFDs = NULL;
        if(exceptfds) {
            tmpExceptFDs = g_new0(fd_set, 1);
            FD_ZERO(tmpExceptFDs);
            g_memmove(tmpExceptFDs, exceptfds, sizeof(fd_set));
        }

        ret = host_select(proc->host, tmpReadFDs, tmpWriteFDs, tmpExceptFDs);

        if(ret == 0) {
            /* we have no events */
            struct timespec forever;
            forever.tv_sec = (__time_t)INT_MAX;
            forever.tv_nsec = 999999999;
            const struct timespec* sleepTime = NULL;

            if(timeout == NULL) {
                /* block indefinitely (until pth wakes us up again) */
                sleepTime = &forever;
            } else if(timeout->tv_sec > 0 || timeout->tv_nsec > 0) {
                /* return after timeout fires */
                sleepTime = timeout;
            } else {
                /* timeout != NULL && timeout->tv_sec == 0 && timeout->tv_nsec == 0 */
                /* return immediately */
                sleepTime = NULL;
            }

            if(sleepTime) {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                utility_assert(proc->tstate == pth_gctx_get());
                pth_nanosleep(sleepTime, NULL);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

                /* ask shadow again */
                if(tmpReadFDs) {
                    FD_ZERO(tmpReadFDs);
                    g_memmove(tmpReadFDs, readfds, sizeof(fd_set));
                }
                if(tmpWriteFDs) {
                    FD_ZERO(tmpWriteFDs);
                    g_memmove(tmpWriteFDs, writefds, sizeof(fd_set));
                }
                if(tmpExceptFDs) {
                    FD_ZERO(tmpExceptFDs);
                    g_memmove(tmpExceptFDs, exceptfds, sizeof(fd_set));
                }

                ret = host_select(proc->host, tmpReadFDs, tmpWriteFDs, tmpExceptFDs);
            }
        }

        if(tmpReadFDs) {
            g_memmove(readfds, tmpReadFDs, sizeof(fd_set));
            g_free(tmpReadFDs);
        }
        if(tmpWriteFDs) {
            g_memmove(writefds, tmpWriteFDs, sizeof(fd_set));
            g_free(tmpWriteFDs);
        }
        if(tmpExceptFDs) {
            g_memmove(exceptfds, tmpExceptFDs, sizeof(fd_set));
            g_free(tmpExceptFDs);
        }
    }

    return ret;
}

static int _process_emu_pollHelper(Process* proc, struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts) {
    gint ret = 0;

    /* this function MUST be called after switching in shadow context */
    utility_assert(proc->activeContext == PCTX_SHADOW);

    if(proc->fdLimit == 0) {
        struct rlimit fdRLimit;
        memset(&fdRLimit, 0, sizeof(struct rlimit));
        if(getrlimit(RLIMIT_NOFILE, &fdRLimit) == 0) {
            proc->fdLimit = (gsize) fdRLimit.rlim_cur;
        }
    }

    if(((gsize)nfds) > proc->fdLimit) {
        _process_setErrno(proc, EINVAL);
        ret = -1;
    } else if(timeout_ts == NULL || timeout_ts->tv_sec != 0 || timeout_ts->tv_nsec != 0) {
        /* either we should block forever, or block until a valid timeout,
         * neither of which shadow supports */
        warning("poll is trying to block, but Shadow doesn't support blocking without pth");
        _process_setErrno(proc, EINTR);
        ret = -1;
    } else {
        ret = host_poll(proc->host, fds, nfds);
        if(ret < 0) {
            _process_setErrno(proc, errno);
        }
    }

    return ret;
}

static int _process_emu_epollCreateHelper(Process* proc, int size, int flags) {
    /* size should be > 0, but can otherwise be completely ignored */
    if(size < 1) {
        _process_setErrno(proc, EINVAL);
        return -1;
    }
    /* the only possible flag is EPOLL_CLOEXEC, which means we should set
     * FD_CLOEXEC on the new file descriptor. just ignore for now. */
    if(flags != 0 && flags != EPOLL_CLOEXEC) {
        _process_setErrno(proc, EINVAL);
        return -1;
    }

    /* switch into shadow and create the new descriptor */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gint handle = host_createDescriptor(proc->host, DT_EPOLL);

    if((flags & EPOLL_CLOEXEC) && (handle > 0)) {
        Descriptor* desc = host_lookupDescriptor(proc->host, handle);
        if(desc) {
            gint options = descriptor_getFlags(desc);
            options |= O_CLOEXEC;
            descriptor_setFlags(desc, options);
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    return handle;
}

static int _process_emu_epollWaitHelper(Process* proc, int epfd, struct epoll_event *events, int maxevents, int timeout) {
    gint ret = 0;

    /* EINVAL if maxevents is less than or equal to zero. */
    if(maxevents <= 0) {
        _process_setErrno(proc, EINVAL);
        ret = -1;
    } else if(timeout != 0) {
        /* either we should block forever, or block until a valid timeout,
         * neither of which shadow supports */
        warning("epoll_wait is trying to block, but Shadow doesn't support blocking without pth");
        _process_setErrno(proc, EINTR);
        ret = -1;
    } else {
        /* switch to shadow context and try to get events if we have any */
        gint nEvents = 0;
        gint result = host_epollGetEvents(proc->host, epfd, events, maxevents, &nEvents);

        if(result != 0) {
            /* there was an error from shadow */
            _process_setErrno(proc, result);
            ret = -1;
        } else {
            ret = nEvents;
        }
    }

    return ret;
}

/* memory allocation family */

void* process_emu_malloc(Process* proc, size_t size) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    void* ptr = malloc(size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(proc->host), ptr, size);
    }
    if(ptr == NULL) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ptr;
}

void* process_emu_calloc(Process* proc, size_t nmemb, size_t size) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    void* ptr = calloc(nmemb, size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(proc->host), ptr, size);
    }
    if(ptr == NULL) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ptr;
}

void* process_emu_realloc(Process* proc, void *ptr, size_t size) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gpointer newptr = realloc(ptr, size);
    if(newptr != NULL) {
        if(ptr == NULL) {
            /* equivalent to malloc */
            if(size) {
                tracker_addAllocatedBytes(host_getTracker(proc->host), newptr, size);
            }
        } else if (size == 0) {
            /* equivalent to free */
            tracker_removeAllocatedBytes(host_getTracker(proc->host), ptr);
        } else {
            /* true realloc */
            tracker_removeAllocatedBytes(host_getTracker(proc->host), ptr);
            if(size) {
                tracker_addAllocatedBytes(host_getTracker(proc->host), newptr, size);
            }
        }
    }

    if(newptr == NULL) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return newptr;
}

void process_emu_free(Process* proc, void *ptr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    free(ptr);
    if(ptr != NULL) {
        tracker_removeAllocatedBytes(host_getTracker(proc->host), ptr);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
}

int process_emu_posix_memalign(Process* proc, void** memptr, size_t alignment, size_t size) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint ret = posix_memalign(memptr, alignment, size);
    if(ret == 0 && size) {
        tracker_addAllocatedBytes(host_getTracker(proc->host), *memptr, size);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

void* process_emu_memalign(Process* proc, size_t blocksize, size_t bytes) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gpointer ptr = memalign(blocksize, bytes);
    if(bytes && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(proc->host), ptr, bytes);
    }
    if(ptr == NULL) {
        _process_setErrno(proc, errno);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ptr;
}

/* aligned_alloc doesnt exist in glibc in the current LTS version of ubuntu */
void* process_emu_aligned_alloc(Process* proc, size_t alignment, size_t size) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gpointer ptr = aligned_alloc(alignment, size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(proc->host), ptr, size);
    }
    if(ptr == NULL) {
        _process_setErrno(proc, errno);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ptr;
}

void* process_emu_valloc(Process* proc, size_t size) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gpointer ptr = valloc(size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(proc->host), ptr, size);
    }
    if(ptr == NULL) {
        _process_setErrno(proc, errno);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ptr;
}

void* process_emu_pvalloc(Process* proc, size_t size) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gpointer ptr = pvalloc(size);
    if(size && ptr != NULL) {
        tracker_addAllocatedBytes(host_getTracker(proc->host), ptr, size);
    }
    if(ptr == NULL) {
        _process_setErrno(proc, errno);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ptr;
}

/* for fd translation */
void* process_emu_mmap(Process* proc, void *addr, size_t length, int prot, int flags,
                  int fd, off_t offset) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    /* anonymous mappings ignore file descriptor */
    if(flags & MAP_ANONYMOUS) {
        gpointer ret = mmap(addr, length, prot, flags, -1, offset);
        if(ret == MAP_FAILED) {
            _process_setErrno(proc, errno);
        }
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return ret;
    }

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("mmap not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gpointer ret = mmap(addr, length, prot, flags, osfd, offset);
            if(ret == MAP_FAILED) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);

    return MAP_FAILED;
}


/* event family */

int process_emu_epoll_create(Process* proc, int size) {
    return _process_emu_epollCreateHelper(proc, size, 0);
}

int process_emu_epoll_create1(Process* proc, int flags) {
    return _process_emu_epollCreateHelper(proc, 1, flags);
}

int process_emu_epoll_ctl(Process* proc, int epfd, int op, int fd, struct epoll_event *event) {
    /*
     * initial checks before passing on to proc->host:
     * EINVAL if fd is the same as epfd, or the requested operation op is not
     * supported by this interface
     */
    if(epfd == fd) {
        _process_setErrno(proc, EINVAL);
        return -1;
    }

    /* switch into shadow and do the operation */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint result = host_epollControl(proc->host, epfd, op, fd, event);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    /*
     * When successful, epoll_ctl() returns zero. When an error occurs,
     * epoll_ctl() returns -1 and errno is set appropriately.
     */
    if(result != 0) {
        _process_setErrno(proc, result);
        return -1;
    } else {
        return 0;
    }
}

int process_emu_epoll_wait(Process* proc, int epfd, struct epoll_event *events, int maxevents, int timeout) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_epoll_wait(epfd, events, maxevents, timeout);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        ret = _process_emu_epollWaitHelper(proc, epfd, events, maxevents, timeout);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_epoll_pwait(Process* proc, int epfd, struct epoll_event *events, int maxevents, int timeout, const sigset_t *ss) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_epoll_pwait(epfd, events, maxevents, timeout, ss);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        /* shadow ignores the sigmask */
        ret = _process_emu_epollWaitHelper(proc, epfd, events, maxevents, timeout);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* socket/io family */

int process_emu_socket(Process* proc, int domain, int type, int protocol) {
    gboolean isNonBlockSet = FALSE;
    gboolean isCloseOnExecuteSet = FALSE;

    /* clear non-blocking flags if set to get true type */
    if(type & SOCK_NONBLOCK) {
        type = type & ~SOCK_NONBLOCK;
        isNonBlockSet = TRUE;
    }
    if(type & SOCK_CLOEXEC) {
        type = type & ~SOCK_CLOEXEC;
        isCloseOnExecuteSet = TRUE;
    }

    gint result = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    /* check inputs for what we support */
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        warning("unsupported socket type \"%i\", we only support SOCK_STREAM and SOCK_DGRAM", type);
        _process_setErrno(proc, EPROTONOSUPPORT);
        result = -1;
    } else if(domain != AF_INET && domain != AF_UNIX) {
        warning("trying to create socket with domain \"%i\", we only support AF_INET and AF_UNIX", domain);
        _process_setErrno(proc, EAFNOSUPPORT);
        result = -1;
    }

    if(result == 0) {
        /* we are all set to create the socket */
        DescriptorType dtype = type == SOCK_STREAM ? DT_TCPSOCKET : DT_UDPSOCKET;
        result = host_createDescriptor(proc->host, dtype);
        Descriptor* desc = host_lookupDescriptor(proc->host, result);

        gint options = descriptor_getFlags(desc);
        if(domain == AF_UNIX) {
            socket_setUnix(((Socket*)desc), TRUE);
        }
        if(isNonBlockSet) {
            options |= O_NONBLOCK;
        }
        if(isCloseOnExecuteSet) {
            options |= O_CLOEXEC;
        }
        descriptor_setFlags(desc, options);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

int process_emu_socketpair(Process* proc, int domain, int type, int protocol, int fds[2]) {
    /* create a pair of connected sockets, i.e. a bi-directional pipe */
    if(domain != AF_UNIX) {
        _process_setErrno(proc, EAFNOSUPPORT);
        return -1;
    }

    gboolean isNonBlockSet = FALSE;
    gboolean isCloseOnExecuteSet = FALSE;

    /* clear non-blocking flags if set to get true type */
    if(type & SOCK_NONBLOCK) {
        type = type & ~SOCK_NONBLOCK;
        isNonBlockSet = TRUE;
    }
    if(type & SOCK_CLOEXEC) {
        type = type & ~SOCK_CLOEXEC;
        isCloseOnExecuteSet = TRUE;
    }

    if(type != SOCK_STREAM) {
        _process_setErrno(proc, EPROTONOSUPPORT);
        return -1;
    }

    gint result = 0;
    gint options = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(result == 0) {
        gint handle = host_createDescriptor(proc->host, DT_SOCKETPAIR);
        // TODO handle could be -1 on error
        fds[0] = handle;
        Descriptor* desc = host_lookupDescriptor(proc->host, handle);
        // TODO desc could be NULL

        options = descriptor_getFlags(desc);
        if(isNonBlockSet) {
            options |= O_NONBLOCK;
        }
        if(isCloseOnExecuteSet) {
            options |= O_CLOEXEC;
        }
        descriptor_setFlags(desc, options);

        Descriptor* linkedDesc = (Descriptor*)channel_getLinkedChannel((Channel*)desc);
        utility_assert(linkedDesc);
        gint linkedHandle = *descriptor_getHandleReference(linkedDesc);
        fds[1] = linkedHandle;

        options = descriptor_getFlags(linkedDesc);
        if(isNonBlockSet) {
            options |= O_NONBLOCK;
        }
        if(isCloseOnExecuteSet) {
            options |= O_CLOEXEC;
        }
        descriptor_setFlags(linkedDesc, options);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

int process_emu_bind(Process* proc, int fd, const struct sockaddr* addr, socklen_t len)  {
    if((addr->sa_family == AF_INET && len < sizeof(struct sockaddr_in)) ||
            (addr->sa_family == AF_UNIX && len < sizeof(struct sockaddr_un))) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return -1;
    }

    return _process_emu_addressHelper(proc, fd, addr, &len, SCT_BIND);
}

int process_emu_getsockname(Process* proc, int fd, struct sockaddr* addr, socklen_t* len)  {
    return _process_emu_addressHelper(proc, fd, addr, len, SCT_GETSOCKNAME);
}

int process_emu_connect(Process* proc, int fd, const struct sockaddr* addr, socklen_t len)  {
    if((addr->sa_family == AF_INET && len < sizeof(struct sockaddr_in)) ||
            (addr->sa_family == AF_UNIX && len < sizeof(struct sockaddr_un))) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return -1;
    }

    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_connect(fd, addr, len);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        ret = _process_emu_addressHelper(proc, fd, addr, &len, SCT_CONNECT);
        _process_changeContext(proc, prevCTX, PCTX_SHADOW);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_getpeername(Process* proc, int fd, struct sockaddr* addr, socklen_t* len)  {
    return _process_emu_addressHelper(proc, fd, addr, len, SCT_GETPEERNAME);
}

ssize_t process_emu_send(Process* proc, int fd, const void *buf, size_t n, int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gssize ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_send(fd, buf, n, flags);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        ret = _process_emu_sendHelper(proc, fd, buf, n, flags, NULL, 0);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_sendto(Process* proc, int fd, const void *buf, size_t n, int flags, const struct sockaddr* addr, socklen_t addr_len)  {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gssize ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_sendto(fd, buf, n, flags, addr, addr_len);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        ret = _process_emu_sendHelper(proc, fd, buf, n, flags, addr, addr_len);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_sendmsg(Process* proc, int fd, const struct msghdr *message, int flags) {
    /* TODO implement */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("sendmsg not implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

ssize_t process_emu_recv(Process* proc, int fd, void *buf, size_t n, int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gssize ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_recv(fd, buf, n, flags);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        ret = _process_emu_recvHelper(proc, fd, buf, n, flags, NULL, 0);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_recvfrom(Process* proc, int fd, void *buf, size_t n, int flags, struct sockaddr* addr, socklen_t *addr_len)  {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gssize ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_recvfrom(fd, buf, n, flags, addr, addr_len);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        ret = _process_emu_recvHelper(proc, fd, buf, n, flags, addr, addr_len);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_recvmsg(Process* proc, int fd, struct msghdr *message, int flags) {
    /* TODO implement */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("recvmsg not implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

int process_emu_getsockopt(Process* proc, int fd, int level, int optname, void* optval, socklen_t* optlen) {
    if(!optlen) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EFAULT);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return -1;
    }

    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    Descriptor* descriptor = host_lookupDescriptor(proc->host, fd);

    gint result = 0;

    /* TODO: implement socket options */
    if(descriptor) {
        if(level == SOL_SOCKET || level == SOL_IP || level == SOL_TCP) {
            DescriptorType t = descriptor_getType(descriptor);
            switch (optname) {
                case TCP_INFO: {
                    if(t == DT_TCPSOCKET) {
                        if(optval) {
                            TCP* tcp = (TCP*)descriptor;
                            tcp_getInfo(tcp, (struct tcp_info *)optval);
                        }
                        *optlen = sizeof(struct tcp_info);
                        result = 0;
                    } else {
                        warning("called getsockopt with TCP_INFO on non-TCP socket");
                        _process_setErrno(proc, ENOPROTOOPT);
                        result = -1;
                    }

                    break;
                }

                case SO_SNDBUF: {
                    if(*optlen < sizeof(gint)) {
                        warning("called getsockopt with SO_SNDBUF with optlen < %i", (gint)(sizeof(gint)));
                        _process_setErrno(proc, EINVAL);
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called getsockopt with SO_SNDBUF on non-socket");
                        _process_setErrno(proc, ENOPROTOOPT);
                        result = -1;
                    } else {
                        if(optval) {
                            *((gint*) optval) = (gint) socket_getOutputBufferSize((Socket*)descriptor);
                        }
                        *optlen = sizeof(gint);
                    }
                    break;
                }

                case SO_RCVBUF: {
                    if(*optlen < sizeof(gint)) {
                        warning("called getsockopt with SO_RCVBUF with optlen < %i", (gint)(sizeof(gint)));
                        _process_setErrno(proc, EINVAL);
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called getsockopt with SO_RCVBUF on non-socket");
                        _process_setErrno(proc, ENOPROTOOPT);
                        result = -1;
                    } else {
                        if(optval) {
                            *((gint*) optval) = (gint) socket_getInputBufferSize((Socket*)descriptor);
                        }
                        *optlen = sizeof(gint);
                    }
                    break;
                }

                case SO_ERROR: {
                    if(optval) {
                        *((gint*)optval) = 0;
                    }
                    *optlen = sizeof(gint);

                    result = 0;
                    break;
                }

                default: {
                    warning("getsockopt optname %i not implemented", optname);
                    _process_setErrno(proc, ENOSYS);
                    result = -1;
                    break;
                }
            }
        } else {
            warning("getsockopt level %i not implemented", level);
            _process_setErrno(proc, ENOSYS);
            result = -1;
        }
    } else {
        _process_setErrno(proc, EBADF);
        result = -1;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

int process_emu_setsockopt(Process* proc, int fd, int level, int optname, const void *optval, socklen_t optlen) {
    if(!optval) {
        _process_setErrno(proc, EFAULT);
        return -1;
    }

    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    Descriptor* descriptor = host_lookupDescriptor(proc->host, fd);

    gint result = 0;

    /* TODO: implement socket options */
    if(descriptor) {
        if(level == SOL_SOCKET) {
            DescriptorType t = descriptor_getType(descriptor);
            switch (optname) {
                case SO_SNDBUF: {
                    if(optlen < sizeof(gint)) {
                        warning("called setsockopt with SO_SNDBUF with optlen < %i", (gint)(sizeof(gint)));
                        _process_setErrno(proc, EINVAL);
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called setsockopt with SO_SNDBUF on non-socket");
                        _process_setErrno(proc, ENOPROTOOPT);
                        result = -1;
                    } else {
                        gint v = *((gint*) optval);
                        socket_setOutputBufferSize((Socket*)descriptor, (gsize)v*2);
                        if(t == DT_TCPSOCKET) {
                            tcp_disableSendBufferAutotuning((TCP*)descriptor);
                        }
                    }
                    break;
                }

                case SO_RCVBUF: {
                    if(optlen < sizeof(gint)) {
                        warning("called setsockopt with SO_RCVBUF with optlen < %i", (gint)(sizeof(gint)));
                        _process_setErrno(proc, EINVAL);
                        result = -1;
                    } else if (t != DT_TCPSOCKET && t != DT_UDPSOCKET) {
                        warning("called setsockopt with SO_RCVBUF on non-socket");
                        _process_setErrno(proc, ENOPROTOOPT);
                        result = -1;
                    } else {
                        gint v = *((gint*) optval);
                        socket_setInputBufferSize((Socket*)descriptor, (gsize)v*2);
                        if(t == DT_TCPSOCKET) {
                            tcp_disableReceiveBufferAutotuning((TCP*)descriptor);
                        }
                    }
                    break;
                }

                case SO_REUSEADDR: {
                    // TODO implement this!
                    // XXX Tor and TGen actually use this option!!
                    debug("setsockopt SO_REUSEADDR not yet implemented");
                    break;
                }

#ifdef SO_REUSEPORT
                case SO_REUSEPORT: {
                    // TODO implement this!
                    // XXX TGen actually uses this option!!
                    debug("setsockopt SO_REUSEPORT not yet implemented");
                    break;
                }
#endif

                case SO_KEEPALIVE: {
                    // TODO implement this!
                    // XXX libevent actually uses this option in evconnlistener_new_bind!!
                    debug("setsockopt SO_KEEPALIVE not yet implemented");
                    break;
                }

                default: {
                    warning("setsockopt optname %i not implemented", optname);
                    _process_setErrno(proc, ENOSYS);
                    result = -1;
                    break;
                }
            }
        } else {
            warning("setsockopt level %i not implemented", level);
            _process_setErrno(proc, ENOSYS);
            result = -1;
        }
    } else {
        _process_setErrno(proc, EBADF);
        result = -1;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

int process_emu_listen(Process* proc, int fd, int n) {
    /* check if this is a socket */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if(!host_isShadowDescriptor(proc->host, fd)){
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        _process_setErrno(proc, EBADF);
        return -1;
    }

    gint result = host_listenForPeer(proc->host, fd, n);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    /* check if there was an error */
    if(result != 0) {
        _process_setErrno(proc, result);
        return -1;
    }

    return 0;
}

int process_emu_accept(Process* proc, int fd, struct sockaddr* addr, socklen_t* addr_len)  {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint ret = 0;

    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_accept(fd, addr, addr_len);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        /* check if this is a virtual socket */
        if(!host_isShadowDescriptor(proc->host, fd)){
            warning("intercepted a non-virtual descriptor");
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            in_addr_t ip = 0;
            in_port_t port = 0;
            gint handle = 0;

            /* direct to proc->host for further checks */
            ret = host_acceptNewPeer(proc->host, fd, &ip, &port, &handle);

            /* check if there was an error */
            if(ret != 0) {
                _process_setErrno(proc, ret);
                ret = -1;
            } else {
                ret = handle;
                if(addr != NULL && addr_len != NULL && *addr_len >= sizeof(struct sockaddr_in)) {
                    struct sockaddr_in* ai = (struct sockaddr_in*) addr;
                    ai->sin_addr.s_addr = ip;
                    ai->sin_port = port;
                    ai->sin_family = AF_INET;
                    *addr_len = sizeof(struct sockaddr_in);
                }
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_accept4(Process* proc, int fd, struct sockaddr* addr, socklen_t* addr_len, int flags)  {
    /* just ignore the flags and call accept */
    if(flags) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        debug("accept4 ignoring flags argument");
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    }
    return process_emu_accept(proc, fd, addr, addr_len);
}

int process_emu_shutdown(Process* proc, int fd, int how) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) {
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return -1;
    }

    /* check if this is a socket */
    gint ret = 0;

    if(!host_isShadowDescriptor(proc->host, fd)){
        /* it's not a shadow descriptor, check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd >= 0) {
            /* probably not a socket, but let the OS set the error */
            ret = shutdown(osfd, how);
            if(ret < 0) {
                _process_setErrno(proc, errno);
            }
        } else {
            _process_setErrno(proc, EBADF);
            ret = -1;
        }
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return ret;
    }

    /* it is a shadow descriptor */
    ret = host_shutdownSocket(proc->host, fd, how);

    if(ret != 0) {
        _process_setErrno(proc, ret);
        ret = -1;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    return ret;
}

ssize_t process_emu_read(Process* proc, int fd, void *buff, size_t numbytes) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gssize ret = 0;

    if(prevCTX == PCTX_PLUGIN && host_isShadowDescriptor(proc->host, fd)) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_read(fd, buff, numbytes);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fread(buff, numbytes, 1, _process_getIOFile(proc, fd));
    } else {
        if(host_isShadowDescriptor(proc->host, fd)){
            Descriptor* desc = host_lookupDescriptor(proc->host, fd);
            if(descriptor_getType(desc) == DT_TIMER) {
                ret = timer_read((Timer*) desc, buff, numbytes);
            } else if (descriptor_getType(desc) == DT_EVENTFD) {
                ret = shd_eventfd_read((EventFD*) desc, buff, numbytes);
            }
            else {
                ret = _process_emu_recvHelper(proc, fd, buff, numbytes, 0, NULL, 0);
            }
        } else if(host_isRandomHandle(proc->host, fd)) {
            Random* random = host_getRandom(proc->host);
            random_nextNBytes(random, (guchar*)buff, numbytes);
            ret = (ssize_t) numbytes;
        } else {
            gint osfd = host_getOSHandle(proc->host, fd);
            if(osfd >= 0) {
                ret = read(osfd, buff, numbytes);
                if(ret < 0) {
                    _process_setErrno(proc, errno);
                }
            } else {
                _process_setErrno(proc, EBADF);
                ret = -1;
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_write(Process* proc, int fd, const void *buff, size_t n) {
    gssize ret = 0;
    if(n == 0) {
        return ret;
    }
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(prevCTX == PCTX_PLUGIN && host_isShadowDescriptor(proc->host, fd)) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_write(fd, buff, n);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fwrite(buff, 1, n, _process_getIOFile(proc, fd));
    } else if(prevCTX == PCTX_PTH && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        /* XXX this hack is to remove rpth's newline char since shadow will add another one */
        if(fd == STDERR_FILENO) {
            error("%.*s", n-1, buff);
        } else {
            debug("%.*s", n-1, buff);
        }
    } else {
        if(host_isShadowDescriptor(proc->host, fd)){
            Descriptor* desc = host_lookupDescriptor(proc->host, fd);
            if(descriptor_getType(desc) == DT_EVENTFD) {
                ret = shd_eventfd_write((EventFD*) desc, buff, n);
            }
            else
                ret = _process_emu_sendHelper(proc, fd, buff, n, 0, NULL, 0);
        } else {
            gint osfd = host_getOSHandle(proc->host, fd);
            if(osfd >= 0) {
                ret = write(osfd, buff, n);
                if(ret < 0) {
                    _process_setErrno(proc, errno);
                }
            } else {
                _process_setErrno(proc, EBADF);
                ret = -1;
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_readv(Process* proc, int fd, const struct iovec *iov, int iovcnt) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gssize ret = 0;

    if(!host_isShadowDescriptor(proc->host, fd)){
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            ret = readv(osfd, iov, iovcnt);
            if(ret < 0) {
                _process_setErrno(proc, errno);
            }
        } else {
            _process_setErrno(proc, EBADF);
            ret = -1;
        }
    } else if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_readv(fd, iov, iovcnt);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        if (iovcnt < 0 || iovcnt > IOV_MAX) {
            _process_setErrno(proc, EINVAL);
            ret = -1;
        } else {
            /* figure out how much they want to read total */
            int i = 0;
            size_t totalIOLength = 0;
            for (i = 0; i < iovcnt; i++) {
                totalIOLength += iov[i].iov_len;
            }

            if (totalIOLength == 0) {
                ret = 0;
            } else {
                /* get a temporary buffer and read to it */
                void* tempBuffer = g_malloc0(totalIOLength);
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                ssize_t totalBytesRead = process_emu_read(proc, fd, tempBuffer, totalIOLength);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);

                if (totalBytesRead > 0) {
                    /* place all of the bytes we read in the iov buffers */
                    size_t bytesCopied = 0;
                    for (i = 0; i < iovcnt; i++) {
                        size_t bytesRemaining = (size_t) (totalBytesRead - bytesCopied);
                        size_t bytesToCopy = MIN(bytesRemaining, iov[i].iov_len);
                        g_memmove(iov[i].iov_base, tempBuffer+bytesCopied, bytesToCopy);
                        bytesCopied += bytesToCopy;
                    }
                }

                g_free(tempBuffer);
                ret = totalBytesRead;
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_writev(Process* proc, int fd, const struct iovec *iov, int iovcnt) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gssize ret = 0;

    if(!host_isShadowDescriptor(proc->host, fd)){
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            ret = writev(osfd, iov, iovcnt);
            if(ret < 0) {
                _process_setErrno(proc, errno);
            }
        } else {
            _process_setErrno(proc, EBADF);
            ret = -1;
        }
    } else if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_writev(fd, iov, iovcnt);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        if(iovcnt < 0 || iovcnt > IOV_MAX) {
            _process_setErrno(proc, EINVAL);
            ret = -1;
        } else {
            /* figure out how much they want to write total */
            int i = 0;
            size_t totalIOLength = 0;
            for(i = 0; i < iovcnt; i++) {
                totalIOLength += iov[i].iov_len;
            }

            if(totalIOLength == 0) {
                ret = 0;
            } else {
                /* get a temporary buffer and write to it */
                void* tempBuffer = g_malloc0(totalIOLength);
                size_t bytesCopied = 0;
                for(i = 0; i < iovcnt; i++) {
                    g_memmove(tempBuffer+bytesCopied, iov[i].iov_base, iov[i].iov_len);
                    bytesCopied += iov[i].iov_len;
                }

                ssize_t totalBytesWritten = 0;
                if(bytesCopied > 0) {
                    /* try to write all of the bytes we got from the iov buffers */
                    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                    totalBytesWritten = process_emu_write(proc, fd, tempBuffer, bytesCopied);
                    _process_changeContext(proc, prevCTX, PCTX_SHADOW);
                }

                g_free(tempBuffer);
                ret = totalBytesWritten;
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_pread(Process* proc, int fd, void *buff, size_t numbytes, off_t offset) {
    gssize ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(prevCTX == PCTX_PLUGIN && host_isShadowDescriptor(proc->host, fd)) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_pread(fd, buff, numbytes, offset);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fread(buff, numbytes, 1, _process_getIOFile(proc, fd));
    } else {
        if(host_isShadowDescriptor(proc->host, fd)){
            warning("pread on shadow file descriptors is not currently supported");
            _process_setErrno(proc, ENOSYS);
            ret = -1;
        } else {
            gint osfd = host_getOSHandle(proc->host, fd);
            if(osfd >= 0) {
                ret = pread(osfd, buff, numbytes, offset);
                if(ret < 0) {
                    _process_setErrno(proc, errno);
                }
            } else {
                _process_setErrno(proc, EBADF);
                ret = -1;
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

ssize_t process_emu_pwrite(Process* proc, int fd, const void *buf, size_t nbytes, off_t offset)
{
    gssize ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(prevCTX == PCTX_PLUGIN && host_isShadowDescriptor(proc->host, fd)) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_pwrite(fd, buf, nbytes, offset);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fwrite(buf, 1, nbytes, _process_getIOFile(proc, fd));
    } else {
        if(host_isShadowDescriptor(proc->host, fd)){
            warning("pwrite on shadow file descriptors is not currently supported");
            _process_setErrno(proc, ENOSYS);
            ret = -1;
        } else {
            gint osfd = host_getOSHandle(proc->host, fd);
            if(osfd >= 0) {
                ret = pwrite(osfd, buf, nbytes, offset);
                if(ret < 0) {
                    _process_setErrno(proc, errno);
                }
            } else {
                _process_setErrno(proc, EBADF);
                ret = -1;
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_close(Process* proc, int fd) {
    /* check if this is a socket */
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(!host_isShadowDescriptor(proc->host, fd)){
        gint ret = 0;
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd == STDOUT_FILENO) {
            if(proc->stdoutFile) {
                ret = fclose(proc->stdoutFile);
                if(ret == EOF) {
                    _process_setErrno(proc, errno);
                }
            }
        } else if (osfd == STDERR_FILENO) {
            if(proc->stderrFile) {
                ret = fclose(proc->stderrFile);
                if(ret == EOF) {
                    _process_setErrno(proc, errno);
                }
            }
        } else if(osfd >= 0) {
            ret = close(osfd);
            if(ret < 0) {
                _process_setErrno(proc, errno);
            }
            host_destroyShadowHandle(proc->host, fd);
        } else {
            _process_setErrno(proc, EBADF);
            ret = -1;
        }
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return ret;
    }

    gint r = host_closeUser(proc->host, fd);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return r;
}

int process_emu_fcntl(Process* proc, int fd, int cmd, void* argp) {
    return _process_emu_fcntlHelper(proc, fd, cmd, argp);
}

int process_emu_ioctl(Process* proc, int fd, unsigned long int request, void* argp) {
    return _process_emu_ioctlHelper(proc, fd, request, argp);
}

int process_emu_pipe2(Process* proc, int pipefds[2], int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint result = 0;

    /* check inputs for what we support */
    if(flags & O_DIRECT) {
        warning("we don't support pipes in 'O_DIRECT' mode, ignoring");
    }

    gint handle = host_createDescriptor(proc->host, DT_PIPE);
    pipefds[0] = handle; /* reader */
    Descriptor* desc = host_lookupDescriptor(proc->host, handle);

    if(desc) {
        gint options = descriptor_getFlags(desc);
        if(flags & O_NONBLOCK) {
            options |= O_NONBLOCK;
        }
        if(flags & O_CLOEXEC) {
            options |= O_CLOEXEC;
        }
        descriptor_setFlags(desc, options);
    }

    Descriptor* linkedDesc = (Descriptor*)channel_getLinkedChannel((Channel*)desc);
    utility_assert(linkedDesc);
    gint linkedHandle = *descriptor_getHandleReference(linkedDesc);
    pipefds[1] = linkedHandle; /* writer */

    if(linkedDesc) {
        gint options = descriptor_getFlags(linkedDesc);
        if(flags & O_NONBLOCK) {
            options |= O_NONBLOCK;
        }
        if(flags & O_CLOEXEC) {
            options |= O_CLOEXEC;
        }
        descriptor_setFlags(linkedDesc, options);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    if(result != 0) {
        _process_setErrno(proc, result);
        return -1;
    }

    return 0;
}

int process_emu_shadow_pipe2(Process* proc, int pipefds[2], int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint result = 0;

    /* check inputs for what we support */
    if(flags & O_DIRECT) {
        warning("we don't support pipes in 'O_DIRECT' mode, ignoring");
    }

    gint handle = host_createDescriptor(proc->host, DT_PIPE);
    pipefds[0] = handle; /* reader */
    Descriptor* desc = host_lookupDescriptor(proc->host, handle);

    if(desc) {
        gint options = descriptor_getFlags(desc);
        if(flags & O_NONBLOCK) {
            options |= O_NONBLOCK;
        }
        if(flags & O_CLOEXEC) {
            options |= O_CLOEXEC;
        }
        descriptor_setFlags(desc, options);
    }

    Descriptor* linkedDesc = (Descriptor*)channel_getLinkedChannel((Channel*)desc);
    utility_assert(linkedDesc);
    gint linkedHandle = *descriptor_getHandleReference(linkedDesc);
    pipefds[1] = linkedHandle; /* writer */
    host_registerShadowChannel(proc->host, linkedHandle);

    if(linkedDesc) {
        gint options = descriptor_getFlags(linkedDesc);
        if(flags & O_NONBLOCK) {
            options |= O_NONBLOCK;
        }
        if(flags & O_CLOEXEC) {
            options |= O_CLOEXEC;
        }
        descriptor_setFlags(linkedDesc, options);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    if(result != 0) {
        _process_setErrno(proc, result);
        return -1;
    }

    return 0;
}

int process_emu_pipe(Process* proc, int pipefds[2]) {
    return process_emu_pipe2(proc, pipefds, O_NONBLOCK);
}

int process_emu_getifaddrs(Process* proc, struct ifaddrs **ifap) {
    if(!ifap) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return -1;
    }

    /* we always have loopback */
    struct ifaddrs *i = g_new0(struct ifaddrs, 1);
    i->ifa_flags = (IFF_UP | IFF_RUNNING | IFF_LOOPBACK);
    i->ifa_name = g_strdup("lo");

    i->ifa_addr = g_new0(struct sockaddr, 1);
    i->ifa_addr->sa_family = AF_INET;
    ((struct sockaddr_in *) i->ifa_addr)->sin_addr.s_addr = address_stringToIP("127.0.0.1");

    /* add the default net address */
    Address* defaultAddress = host_getDefaultAddress(proc->host);
    if(defaultAddress != NULL) {
        struct ifaddrs *j = g_new0(struct ifaddrs, 1);
        j->ifa_flags = (IFF_UP | IFF_RUNNING);
        j->ifa_name = g_strdup("eth0");

        j->ifa_addr = g_new0(struct sockaddr, 1);
        j->ifa_addr->sa_family = AF_INET;
        ((struct sockaddr_in *) j->ifa_addr)->sin_addr.s_addr = (in_addr_t)address_toNetworkIP(defaultAddress);

        i->ifa_next = j;
    }

    *ifap = i;
    return 0;
}

void process_emu_freeifaddrs(Process* proc, struct ifaddrs *ifa) {
    struct ifaddrs* iter = ifa;
    while(iter != NULL) {
        struct ifaddrs* next = iter->ifa_next;
        if(iter->ifa_addr) {
            g_free(iter->ifa_addr);
        }
        if(iter->ifa_name) {
            g_free(iter->ifa_name);
        }
        g_free(iter);
        iter = next;
    }
}

/* polling */

unsigned int process_emu_sleep(Process* proc, unsigned int sec) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    unsigned int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_sleep(sec);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("sleep() not currently implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_usleep(Process* proc, unsigned int sec) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_usleep(sec);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("usleep() not currently implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_nanosleep(Process* proc, const struct timespec *rqtp, struct timespec *rmtp) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_nanosleep(rqtp, rmtp);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("nanosleep() not currently implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_select(Process* proc, int nfds, fd_set *readfds, fd_set *writefds,
                    fd_set *exceptfds, struct timeval *timeout) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_select(nfds, readfds, writefds, exceptfds, timeout);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        struct timespec timeout_ts;
        timeout_ts.tv_sec = timeout->tv_sec;
        timeout_ts.tv_nsec = (__syscall_slong_t)(timeout->tv_usec * 1000);
        ret = _process_emu_selectHelper(proc, nfds, readfds, writefds, exceptfds, &timeout_ts);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pselect(Process* proc, int nfds, fd_set *readfds, fd_set *writefds,
                    fd_set *exceptfds, const struct timespec *timeout, const sigset_t *sigmask) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_pselect(nfds, readfds, writefds, exceptfds, timeout, sigmask);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        ret = _process_emu_selectHelper(proc, nfds, readfds, writefds, exceptfds, timeout);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_poll(Process* proc, struct pollfd *pfd, nfds_t nfd, int timeout) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_poll(pfd, nfd, timeout);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        struct timespec timeout_ts;
        timeout_ts.tv_sec = timeout / 1000;
        timeout_ts.tv_nsec = (__syscall_slong_t)((timeout % 1000) * 100000);
        ret = _process_emu_pollHelper(proc, pfd, nfd, &timeout_ts);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_ppoll(Process* proc, struct pollfd *fds, nfds_t nfds, const struct timespec *timeout_ts, const sigset_t *sigmask) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_ppoll(fds, nfds, timeout_ts, sigmask);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        ret = _process_emu_pollHelper(proc, fds, nfds, timeout_ts);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

pid_t process_emu_fork(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    pid_t ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_fork();
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("fork() not currently implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_system(Process* proc, const char *cmd) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_system(cmd);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("system() not currently implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_sigwait(Process* proc, const sigset_t *set, int *sig) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_sigwait(set, sig);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("sigwait() not currently implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

pid_t process_emu_waitpid(Process* proc, pid_t pid, int *status, int options) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    pid_t ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        ret = pth_waitpid(pid, status, options);
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("waitpid() not currently implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* timers */

int process_emu_eventfd(Process* proc, int initval, int flags) {
    // Implementation changed by Yonggon Kim on 20. 07. 01.
    // Tests are implemented in BLEEP repository regression test directory (regtest/shadow-syscalls/1_eventfd)
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gint result = host_createDescriptor(proc->host, DT_EVENTFD);

    if(result > 0) {
        Descriptor* desc = host_lookupDescriptor(proc->host, result);
        if(desc) {
            gint options = descriptor_getFlags(desc);
            if(flags & EFD_NONBLOCK) {
                options |= O_NONBLOCK;
            }
            if(flags & EFD_CLOEXEC) {
                options |= O_CLOEXEC;
            }
            if(flags & EFD_SEMAPHORE) {
                warning("EFD_SEMAPHORE option is not implemented for Shadow eventfd");
            }
            descriptor_setFlags(desc, options);

            eventfd_setInitVal((EventFD*)desc, initval);
        }
    }
    if(result < 0) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

//    gint result = 0;
//
//    gint osfd = eventfd(initval, flags);
//    if(osfd == -1) {
//        _process_setErrno(proc, errno);
//    }
//
//    gint shadowfd = osfd >= 3 ? host_createShadowHandle(proc->host, osfd) : osfd;
//
//    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
//    result = shadowfd;
    return result;
}

int process_emu_timerfd_create(Process* proc, int clockid, int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gint result = host_createDescriptor(proc->host, DT_TIMER);
    if(result > 0) {
        Descriptor* desc = host_lookupDescriptor(proc->host, result);
        if(desc) {
            gint options = descriptor_getFlags(desc);
            if(flags & TFD_NONBLOCK) {
                options |= O_NONBLOCK;
            }
            if(flags & TFD_CLOEXEC) {
                options |= O_CLOEXEC;
            }
            descriptor_setFlags(desc, options);
        }
    }
    if(result < 0) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    return result;
}

int process_emu_timerfd_settime(Process* proc, int fd, int flags,
                           const struct itimerspec *new_value,
                           struct itimerspec *old_value) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint ret = 0;

    Descriptor* desc = host_lookupDescriptor(proc->host, fd);
    if(!desc) {
        _process_setErrno(proc, EBADF);
        ret = -1;
    } else if(descriptor_getType(desc) != DT_TIMER) {
        _process_setErrno(proc, EINVAL);
        ret = -1;
    } else {
        ret = timer_setTime((Timer*)desc, flags, new_value, old_value);
        if(ret < 0) {
            _process_setErrno(proc, errno);
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_timerfd_gettime(Process* proc, int fd, struct itimerspec *curr_value) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint ret = 0;

    Descriptor* desc = host_lookupDescriptor(proc->host, fd);
    if(!desc) {
        _process_setErrno(proc, EBADF);
        ret = -1;
    } else if(descriptor_getType(desc) != DT_TIMER) {
        _process_setErrno(proc, EINVAL);
        ret = -1;
    } else {
        ret = timer_getTime((Timer*)desc, curr_value);
        if(ret < 0) {
            _process_setErrno(proc, errno);
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* plugin event log */
int process_emu_shadow_push_eventlog(Process* proc, const char *s) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;

    const gchar* hostname = host_getName(proc->host);
    message("shadow_push_eventlog:%s,%llu,%s", hostname, worker_getCurrentTime(), s);


    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}


/* file specific */

int process_emu_fileno(Process* proc, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gint osfd = fileno(stream);
    if(osfd == -1) {
        _process_setErrno(proc, errno);
    }
    gint shadowfd = host_getShadowHandle(proc->host, osfd);

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return (shadowfd >= 0) ? shadowfd : osfd;
}

int process_emu_open(Process* proc, const char *pathname, int flags, mode_t mode) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int result = 0;

    if(prevCTX == PCTX_PLUGIN && g_ascii_strncasecmp(pathname, "/etc/localtime", 14) == 0) {
        /* return error, glib will use UTC time */
        result = -1;
        _process_setErrno(proc, EEXIST);
    } else {
        gint osfd = open(pathname, flags, mode);
        if(osfd == -1) {
            _process_setErrno(proc, errno);
        }
        gint shadowfd = osfd >= 3 ? host_createShadowHandle(proc->host, osfd) : osfd;

        if(utility_isRandomPath((gchar*)pathname)) {
            host_setRandomHandle(proc->host, shadowfd);
        }

        result = shadowfd;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

int process_emu_open64(Process* proc, const char *pathname, int flags, mode_t mode) {
    return process_emu_open(proc, pathname, flags, mode);
}

int process_emu_creat(Process* proc, const char *pathname, mode_t mode) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gint osfd = creat(pathname, mode);
    if(osfd == -1) {
        _process_setErrno(proc, errno);
    }
    gint shadowfd = osfd >= 3 ? host_createShadowHandle(proc->host, osfd) : osfd;

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return shadowfd;
}

FILE *process_emu_fopen(Process* proc, const char *path, const char *mode) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    FILE* osfile = NULL;
    if(prevCTX == PCTX_PLUGIN && g_ascii_strncasecmp(path, "/etc/localtime", 14) == 0) {
        /* return error, glib will use UTC time */
        _process_setErrno(proc, EEXIST);
    } else {
        osfile = fopen(path, mode);
        if(osfile == NULL) {
            _process_setErrno(proc, errno);
        }
        if(osfile) {
            gint osfd = fileno(osfile);
            if(osfd == -1) {
                _process_setErrno(proc, errno);
            }
            gint shadowfd = osfd >= 3 ? host_createShadowHandle(proc->host, osfd) : osfd;

            if(utility_isRandomPath((gchar*)path)) {
                host_setRandomHandle(proc->host, shadowfd);
            }
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return osfile;
}

FILE *process_emu_fopen64(Process* proc, const char *path, const char *mode) {
    return process_emu_fopen(proc, path, mode);
}

FILE *process_emu_fmemopen(Process* proc, void* buf, size_t size, const char *mode) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    FILE* osfile = fmemopen(buf, size, mode);
    if(osfile == NULL) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return osfile;
}

FILE *process_emu_open_memstream(Process* proc, char **ptr, size_t *sizeloc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    FILE* osfile = open_memstream(ptr, sizeloc);
    if(osfile == NULL) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return osfile;
}

FILE *process_emu_open_wmemstream(Process* proc, wchar_t **ptr, size_t *sizeloc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    FILE* osfile = open_wmemstream(ptr, sizeloc);
    if(osfile == NULL) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return osfile;
}

FILE *process_emu_fdopen(Process* proc, int fd, const char *mode) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fdopen not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            FILE* osfile = fdopen(osfd, mode);
            if(osfile == NULL) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return osfile;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return NULL;
}

int process_emu_dup(Process* proc, int oldfd) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, oldfd)) {
        warning("dup not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfdOld = host_getOSHandle(proc->host, oldfd);
        if (osfdOld >= 0) {
            gint osfd = dup(osfdOld);
            if(osfd == -1) {
                _process_setErrno(proc, errno);
            }
            gint shadowfd = osfd >= 3 ? host_createShadowHandle(proc->host, osfd) : osfd;
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return osfd;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_dup2(Process* proc, int oldfd, int newfd) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, oldfd) || host_isShadowDescriptor(proc->host, newfd)) {
        warning("dup2 not implemented for Shadow descriptor types");
    } else {
        /* check if we have mapped os fds */
        gint osfdOld = host_getOSHandle(proc->host, oldfd);
        gint osfdNew = host_getOSHandle(proc->host, newfd);

        /* if the newfd is not mapped, then we need to map it later */
        gboolean isMapped = osfdNew >= 3 ? TRUE : FALSE;
        osfdNew = osfdNew == -1 ? newfd : osfdNew;

        if (osfdOld >= 0) {
            gint osfd = dup2(osfdOld, osfdNew);
            if(osfd == -1) {
                _process_setErrno(proc, errno);
            }

            gint shadowfd = !isMapped && osfd >= 3 ? host_createShadowHandle(proc->host, osfd) : osfd;

            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return shadowfd;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_dup3(Process* proc, int oldfd, int newfd, int flags) {
    if(oldfd == newfd) {
        _process_setErrno(proc, EINVAL);
        return -1;
    }

    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, oldfd) || host_isShadowDescriptor(proc->host, newfd)) {
        warning("dup3 not implemented for Shadow descriptor types");
    } else {
        /* check if we have mapped os fds */
        gint osfdOld = host_getOSHandle(proc->host, oldfd);
        gint osfdNew = host_getOSHandle(proc->host, newfd);

        /* if the newfd is not mapped, then we need to map it later */
        gboolean isMapped = osfdNew >= 3 ? TRUE : FALSE;
        osfdNew = osfdNew == -1 ? newfd : osfdNew;

        if (osfdOld >= 0) {
            gint osfd = dup3(osfdOld, osfdNew, flags);
            if(osfd == -1) {
                _process_setErrno(proc, errno);
            }

            gint shadowfd = !isMapped && osfd >= 3 ? host_createShadowHandle(proc->host, osfd) : osfd;

            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return shadowfd;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_fclose(Process* proc, FILE *fp) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gint osfd = fileno(fp);
    gint shadowHandle = osfd >= 0 ? host_getShadowHandle(proc->host, osfd) : -1;

    gint ret = fclose(fp);
    if(ret == EOF) {
        _process_setErrno(proc, errno);
    }

    if(shadowHandle >= 0) {
        host_destroyShadowHandle(proc->host, shadowHandle);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fseek(Process* proc, FILE *stream, long offset, int whence) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    int ret = fseek(stream, offset, whence);

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

long process_emu_ftell(Process* proc, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    long ret = ftell(stream);

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

void process_emu_rewind(Process* proc, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    rewind(stream);

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
}

int process_emu_fgetpos(Process* proc, FILE *stream, fpos_t *pos) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    int ret = fgetpos(stream, pos);

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fsetpos(Process* proc, FILE *stream, const fpos_t *pos) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    int ret = fsetpos(stream, pos);

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* fstat redirects to this */
int process_emu___fxstat (Process* proc, int ver, int fd, struct stat *buf) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fstat not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = fstat(osfd, buf);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

/* fstat64 redirects to this */
int process_emu___fxstat64 (Process* proc, int ver, int fd, struct stat64 *buf) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fstat64 not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = fstat64(osfd, buf);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_fstatfs (Process* proc, int fd, struct statfs *buf) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fstatfs not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = fstatfs(osfd, buf);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_fstatfs64 (Process* proc, int fd, struct statfs64 *buf) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fstatfs64 not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = fstatfs64(osfd, buf);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

off_t process_emu_lseek(Process* proc, int fd, off_t offset, int whence) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("lseek not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            off_t ret = lseek(osfd, offset, whence);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return (off_t)-1;
}

off64_t process_emu_lseek64(Process* proc, int fd, off64_t offset, int whence) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("lseek64 not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            off_t ret = lseek64(osfd, offset, whence);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return (off64_t)-1;
}

int process_emu_flock(Process* proc, int fd, int operation) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("flock not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = flock(osfd, operation);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return (off_t)-1;
}

int process_emu_fsync(Process* proc, int fd) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        FILE* f = _process_getIOFile(proc, fd);
        ret = fsync(fileno(f));
        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fsync not implemented for Shadow descriptor types");
        _process_setErrno(proc, EBADF);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            ret = fsync(osfd);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        } else {
            _process_setErrno(proc, EBADF);
            ret = -1;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_ftruncate(Process* proc, int fd, off_t length) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("ftruncate not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = ftruncate(osfd, length);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_ftruncate64(Process* proc, int fd, off64_t length) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("ftruncate64 not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = ftruncate64(osfd, length);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_posix_fallocate(Process* proc, int fd, off_t offset, off_t len) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("posix_fallocate not implemented for Shadow descriptor types");
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if (osfd >= 0) {
            gint ret = posix_fallocate(osfd, offset, len);
            _process_changeContext(proc, PCTX_SHADOW, prevCTX);
            return ret;
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    _process_setErrno(proc, EBADF);
    return -1;
}

int process_emu_fstatvfs(Process* proc, int fd, struct statvfs *buf) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fstatvfs not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fstatvfs(osfd, buf);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fdatasync(Process* proc, int fd) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fdatasync not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fdatasync(osfd);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_syncfs(Process* proc, int fd) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("syncfs not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = syncfs(osfd);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fallocate(Process* proc, int fd, int mode, off_t offset, off_t len) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fallocate not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fallocate(osfd, mode, offset, len);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fexecve(Process* proc, int fd, char *const argv[], char *const envp[]) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fexecve not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fexecve(osfd, argv, envp);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

long process_emu_fpathconf(Process* proc, int fd, int name) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fpathconf not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fpathconf(osfd, name);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fchdir(Process* proc, int fd) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fchdir not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fchdir(osfd);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fchown(Process* proc, int fd, uid_t owner, gid_t group) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fchown not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fchown(osfd, owner, group);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fchmod(Process* proc, int fd, mode_t mode) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("fchmod not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = fchmod(osfd, mode);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_posix_fadvise(Process* proc, int fd, off_t offset, off_t len, int advice) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("posix_fadvise not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = posix_fadvise(osfd, offset, len, advice);
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_lockf(Process* proc, int fd, int cmd, off_t len) {
    int ret = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if (host_isShadowDescriptor(proc->host, fd)) {
        warning("lockf not implemented for Shadow descriptor types");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    } else {
        /* check if we have a mapped os fd */
        gint osfd = host_getOSHandle(proc->host, fd);
        if(osfd < 0) {
            _process_setErrno(proc, EBADF);
            ret = -1;
        } else {
            ret = lockf(osfd, cmd, len);
            if(ret == -1) {
                _process_setErrno(proc, errno);
            }
        }
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_openat(Process* proc, int dirfd, const char *pathname, int flags, mode_t mode) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("openat not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

int process_emu_faccessat(Process* proc, int dirfd, const char *pathname, int mode, int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("faccessat not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

int process_emu_unlinkat(Process* proc, int dirfd, const char *pathname, int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("unlinkat not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

int process_emu_fchmodat(Process* proc, int dirfd, const char *pathname, mode_t mode, int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("fchmodat not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

int process_emu_fchownat(Process* proc, int dirfd, const char *pathname, uid_t owner, gid_t group, int flags) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("fchownat not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

size_t process_emu_fread(Process* proc, void *ptr, size_t size, size_t nmemb, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    size_t ret;

    /* get the fd the operating system uses to refer to this FILE stream */
    int osfd = fileno(stream);

    if(prevCTX == PCTX_PLUGIN) {
        /* if the plugin is trying to read from an std stream, redirect to our process log file */
        if(osfd == STDOUT_FILENO || osfd == STDERR_FILENO) {
            FILE* stdioFile = _process_getIOFile(proc, osfd);
            ret = fread(ptr, size, nmemb, stdioFile);
            fflush(stdioFile);
        } else {
            /* If shadow has an FD, then it knows about the FILE - it created the FILE
             * stream to service another syscall, but then it mapped a "virtual" shadowFD to
             * return to the plugin so the plugin would use shadowFD for future operations. */
            gint shadowFD = host_getShadowHandle(proc->host, osfd);
            if(shadowFD >= 0) {
                /* ok so shadow knows about the file. if this is a true shadow-backed
                 * descriptor like a socket, then we should never have associated
                 * a FILE stream with it. */
                if(host_isShadowDescriptor(proc->host, shadowFD)) {
                    error("A file stream with an os fd %i was associated with a "
                            "shadow descriptor with a shadow fd %i", osfd, shadowFD);
                }

                /* if this is a random file, then we can return bytes here */
                if(host_isRandomHandle(proc->host, shadowFD)) {
                    gsize numBytes = size * nmemb;
                    Random* random = host_getRandom(proc->host);
                    random_nextNBytes(random, (guchar*)ptr, numBytes);
                    ret = nmemb;
                } else {
                    /* shadow knows about the file, but it is an os-backed file and
                     * osfd is the actual thing we should read from */
                    ret = fread(ptr, size, nmemb, stream);
                }
            } else {
                /* This is a file that shadow does not know about, and never mapped it to
                 * a virtual shadow fd for the process. This is valid in case of fmemopen,
                 * which returns an osfd of -1 since there is no file backing it. */
                info("fread() was called on file stream with fd %i, and shadow never mapped it", osfd);
                ret = fread(ptr, size, nmemb, stream);
            }
        }
    } else {
        /* fread is being called from pth or shadow */
        ret = fread(ptr, size, nmemb, stream);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

size_t process_emu_fwrite(Process* proc, const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    size_t ret;

    int fd = fileno(stream);
    if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fwrite(ptr, size, nmemb, _process_getIOFile(proc, fd));
    } else {
        ret = fwrite(ptr, size, nmemb, stream);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fputc(Process* proc, int c, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret;

    int fd = fileno(stream);
    if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fputc(c, _process_getIOFile(proc, fd));
    } else {
        ret = fputc(c, stream);
    }

    if(ret == EOF) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fputs(Process* proc, const char *s, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret;

    int fd = fileno(stream);
    if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fputs(s, _process_getIOFile(proc, fd));
    } else {
        ret = fputs(s, stream);
    }

    if(ret == EOF) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_putchar(Process* proc, int c) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret;

    if(prevCTX == PCTX_PLUGIN) {
        ret = fputc(c, _process_getIOFile(proc, STDOUT_FILENO));
    } else {
        ret = putchar(c);
    }

    if(ret == EOF) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_puts(Process* proc, const char *s) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret;

    if(prevCTX == PCTX_PLUGIN) {
        ret = fputs(s, _process_getIOFile(proc, STDOUT_FILENO));
        if(ret >= 0) {
            ret = fputs("\n", _process_getIOFile(proc, STDOUT_FILENO));
        }
    } else {
        ret = puts(s);
    }

    if(ret == EOF) {
        _process_setErrno(proc, errno);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_vprintf(Process* proc, const char *format, va_list ap) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = g_vfprintf(_process_getIOFile(proc, STDOUT_FILENO), format, ap);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_vfprintf(Process* proc, FILE *stream, const char *format, va_list ap) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret;

    int fd = fileno(stream);
    if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = g_vfprintf(_process_getIOFile(proc, fd), format, ap);
    } else {
        ret = g_vfprintf(stream, format, ap);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_fflush(Process* proc, FILE *stream) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret;

    int fd = fileno(stream);
    if(prevCTX == PCTX_PLUGIN && (fd == STDOUT_FILENO || fd == STDERR_FILENO)) {
        ret = fflush(_process_getIOFile(proc, fd));
    } else {
        ret = fflush(stream);
    }

    if(ret == EOF) {
        _process_setErrno(proc, errno);
    }

    /* flush program output */
    if(proc->stdoutFile) {
        fflush(proc->stdoutFile);
    }
    if(proc->stderrFile) {
        fflush(proc->stderrFile);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* time family */

/* make sure we return the 'emulated' time, and not the actual simulation clock */
EmulatedTime _process_getEmulatedTimeHelper(Process* proc) {
    return worker_getEmulatedTime();
}

time_t process_emu_time(Process* proc, time_t *t)  {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    EmulatedTime now = _process_getEmulatedTimeHelper(proc);
    time_t secs = (time_t) (now / SIMTIME_ONE_SECOND);
    if(t != NULL){
        *t = secs;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return secs;
}

int process_emu_clock_gettime(Process* proc, clockid_t clk_id, struct timespec *tp) {
    if(tp == NULL) {
        _process_setErrno(proc, EFAULT);
        return -1;
    }

    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    EmulatedTime now = _process_getEmulatedTimeHelper(proc);
    tp->tv_sec = now / SIMTIME_ONE_SECOND;
    tp->tv_nsec = now % SIMTIME_ONE_SECOND;

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return 0;
}

int process_emu_gettimeofday(Process* proc, struct timeval* tv, struct timezone* tz) {
    if(tv) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

        EmulatedTime now = _process_getEmulatedTimeHelper(proc);
        EmulatedTime sec = now / (EmulatedTime)SIMTIME_ONE_SECOND;
        EmulatedTime usec = (now - (sec*(EmulatedTime)SIMTIME_ONE_SECOND)) / (EmulatedTime)SIMTIME_ONE_MICROSECOND;
        utility_assert(usec < (EmulatedTime)1000000);
        tv->tv_sec = (time_t)sec;
        tv->tv_usec = (suseconds_t)usec;

        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    }
    return 0;
}

struct tm* process_emu_localtime(Process* proc, const time_t *timep) {
    return process_emu_localtime_r(proc, timep, &proc->timeBuffer);
}

struct tm* process_emu_localtime_r(Process* proc, const time_t *timep, struct tm *result) {
    /* return time relative to UTC so SimTime 0 corresponds to Jan 1 1970 */
    return gmtime_r(timep, result);
}


/* name/address family */


int process_emu_gethostname(Process* proc, char* name, size_t len) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint result = -1;

//  in_addr_t ip = proc->host_getDefaultIP(proc->host);
//  const gchar* hostname = internetwork_resolveID(worker_getPrivate()->cached_engine->internet, (GQuark)ip);

    if(name != NULL && proc->host != NULL) {
        /* resolve my address to a hostname */
        const gchar* sysname = host_getName(proc->host);

        if(sysname != NULL && len > strlen(sysname)) {
            if(strncpy(name, sysname, len) != NULL) {
                result = 0;
            }
        }
    }

    _process_setErrno(proc, EFAULT);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

int process_emu_getaddrinfo(Process* proc, const char *name, const char *service,
        const struct addrinfo *hints, struct addrinfo **res) {
    if(name == NULL && service == NULL) {
        _process_setErrno(proc, EINVAL);
        return EAI_NONAME;
    }

    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gint result = 0;
    *res = NULL;

    in_addr_t ip = INADDR_NONE;
    in_port_t port = 0;

    if(name == NULL) {
        if(hints && (hints->ai_flags & AI_PASSIVE)) {
            ip = htonl(INADDR_ANY);
        } else {
            ip = htonl(INADDR_LOOPBACK);
        }
    } else if(!g_ascii_strncasecmp(name, "localhost", 9) || !g_ascii_strncasecmp(name, "127.0.0.1", 9)) {
        ip = htonl(INADDR_LOOPBACK);
    } else {
        Address* address = NULL;

        /* name may be a number-and-dots address, or a hostname. lets find out which. */
        ip = address_stringToIP(name);

        if(ip == INADDR_NONE) {
            /* if AI_NUMERICHOST, don't do hostname lookup */
            if(!hints || (hints && !(hints->ai_flags & AI_NUMERICHOST))) {
                /* the name string is not a dots-and-decimals string, try it as a hostname */
                address = dns_resolveNameToAddress(worker_getDNS(), name);
            }
        } else {
            /* we got an ip from the string, so lookup by the ip */
            address = dns_resolveIPToAddress(worker_getDNS(), ip);
        }

        if(address) {
            /* found it */
            ip = address_toNetworkIP(address);
        } else {
            /* at this point it is an error */
            ip = INADDR_NONE;
            _process_setErrno(proc, EINVAL);
            result = EAI_NONAME;
        }
    }

    if(service) {
        /* get the service name if possible */
        if(!hints || (hints && !(hints->ai_flags & AI_NUMERICSERV))) {
            /* XXX this is not thread safe! */
            struct servent* serviceEntry = getservbyname(service, NULL);
            if(serviceEntry) {
                /* this is in network order */
                port = (in_port_t) serviceEntry->s_port;
            }
        }

        /* if not found, try converting string directly to port */
        if(port == 0) {
            /* make sure we have network order */
            port = (in_port_t)htons((uint16_t)strtol(service, NULL, 10));
        }
    }

    if(ip != INADDR_NONE) {
        /* should have address now */
        struct sockaddr_in* sa = g_malloc(sizeof(struct sockaddr_in));
        /* application will expect it in network order */
        // sa->sin_addr.s_addr = (in_addr_t) htonl((guint32)(*addr));
        sa->sin_addr.s_addr = ip;
        sa->sin_family = AF_INET; /* libcurl expects this to be set */
        sa->sin_port = port;

        struct addrinfo* ai_out = g_malloc(sizeof(struct addrinfo));
        ai_out->ai_addr = (struct sockaddr*) sa;
        ai_out->ai_addrlen =  sizeof(struct sockaddr_in);
        ai_out->ai_canonname = NULL;
        ai_out->ai_family = AF_INET;
        ai_out->ai_flags = 0;
        ai_out->ai_next = NULL;
        ai_out->ai_protocol = 0;
        ai_out->ai_socktype = SOCK_STREAM;

        *res = ai_out;
        result = 0;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return result;
}

void process_emu_freeaddrinfo(Process* proc, struct addrinfo *res) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if(res && res->ai_addr != NULL) {
        g_free(res->ai_addr);
        res->ai_addr = NULL;
        g_free(res);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
}

int process_emu_getnameinfo(Process* proc, const struct sockaddr* sa, socklen_t salen,
        char * host, socklen_t hostlen, char *serv, socklen_t servlen,
        /* glibc-headers changed type of the flags, and then changed back */
#if (__GLIBC__ > 2 || (__GLIBC__ == 2 && (__GLIBC_MINOR__ < 2 || __GLIBC_MINOR__ > 13)))
        int flags) {
#else
        unsigned int flags) {
#endif

    /* FIXME this is not fully implemented */
    if(!sa) {
        return EAI_FAIL;
    }

    gint retval = 0;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    guint32 convertedIP = (guint32) (((struct sockaddr_in*)sa)->sin_addr.s_addr);
    char buf[33];
    //Modified for BLEEP Response Port Error
    if(serv != NULL) {
        unsigned short nPort = ((struct sockaddr_in *) sa)->sin_port;
        guint32 convertedPort = (guint32) ntohs(nPort);
        memset(buf, 0, sizeof(char)*33);
        g_sprintf(buf, "%d", convertedPort);
        memcpy(serv, buf, servlen);
    }

    Address* address = dns_resolveIPToAddress(worker_getDNS(), convertedIP);

    if(address != NULL) {
        gchar* hostname = (flags & NI_NUMERICHOST) ? address_toHostIPString(address) : address_toHostName(address);
        if(hostname != NULL && host != NULL) {
            g_utf8_strncpy(host, hostname, hostlen);
        } else {
            retval = EAI_FAIL;
        }
    } else {
        retval = EAI_NONAME;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return retval;
}

struct hostent* process_emu_gethostbyname(Process* proc, const gchar* name) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("gethostbyname not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return NULL;
}

int process_emu_gethostbyname_r(Process* proc, const gchar *name, struct hostent *ret, gchar *buf,
        gsize buflen, struct hostent **result, gint *h_errnop) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("gethostbyname_r not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

struct hostent* process_emu_gethostbyname2(Process* proc, const gchar* name, gint af) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("gethostbyname2 not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return NULL;
}

int process_emu_gethostbyname2_r(Process* proc, const gchar *name, gint af, struct hostent *ret,
        gchar *buf, gsize buflen, struct hostent **result, gint *h_errnop) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("gethostbyname2_r not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

struct hostent* process_emu_gethostbyaddr(Process* proc, const void* addr, socklen_t len, gint type) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("gethostbyaddr not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return NULL;
}

int process_emu_gethostbyaddr_r(Process* proc, const void *addr, socklen_t len, gint type,
        struct hostent *ret, char *buf, gsize buflen, struct hostent **result,
        gint *h_errnop) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("gethostbyaddr_r not yet implemented");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return -1;
}

/* random family */

int process_emu_rand(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint r = random_rand(host_getRandom(proc->host));
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return r;
}

int process_emu_rand_r(Process* proc, unsigned int *seedp) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint r = random_rand(host_getRandom(proc->host));
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return r;
}

void process_emu_srand(Process* proc, unsigned int seed) {
    return;
}

long int process_emu_random(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    gint r = random_rand(host_getRandom(proc->host));
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return (long int)r;
}

int process_emu_random_r(Process* proc, struct random_data *buf, int32_t *result) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    utility_assert(result != NULL);
    *result = (int32_t)random_rand(host_getRandom(proc->host));
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return 0;
}

void process_emu_srandom(Process* proc, unsigned int seed) {
    return;
}

int process_emu_srandom_r(Process* proc, unsigned int seed, struct random_data *buf) {
    return 0;
}

/* signals */

int process_emu_sigaction(Process* proc, int signum, const struct sigaction* action, struct sigaction* oldaction) {
    if(signum == SIGSEGV || signum == SIGFPE || signum == SIGABRT || signum == SIGILL) {
        /* ignore plugin attempts to install signal handlers for the deadly signals*/
        /* TODO really, we should save any handler that the plugin is trying to set, then if
         * a deadly signal occurs, set a flag and let the plugin try to correct it once, and if
         * the signal is triggered a second time, then abort the process. /TODO
         */
        return 0;
    } else if (proc->plugin.sigaction == NULL)
        return 0;
    else {
        /* allow the plugin to set handlers for other signals */
        return proc->plugin.sigaction(signum, action, oldaction);
    }
}

/* exit family */

static void _process_exitHelper(Process* proc, void *value_ptr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        /* get the name of the currently running pth thread */
        char* pthThreadName;
        pth_attr_t threadAttr = pth_attr_of(pth_self());
        pth_attr_get(threadAttr, PTH_ATTR_NAME, &pthThreadName);
        pth_attr_destroy(threadAttr);

        /* switch back to shadow to log a useful message */
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        warning("thread '%s' in process '%s' will be terminated by pth", pthThreadName, _process_getName(proc));
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

        pth_exit(value_ptr);

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_exit() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
}

void process_emu_exit(Process* proc, int status) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("exit() was called in process '%s'", _process_getName(proc));
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    proc->returnCode = status;
    _process_exitHelper(proc, NULL);
}

void process_emu_abort(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    critical("abort() was called in process '%s'", _process_getName(proc));
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    proc->returnCode = 128 + SIGABRT;
    _process_exitHelper(proc, NULL);
}

int process_emu_on_exit(Process* proc, void (*function)(int , void *), void *arg) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gboolean success = FALSE;
    if(proc) {
        success = process_addAtExitCallback(proc, function, arg, TRUE);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return success == TRUE ? 0 : -1;
}

int process_emu_atexit(Process* proc, void (*func)(void)) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gboolean success = FALSE;
    if(proc) {
        success = process_addAtExitCallback(proc, func, NULL, FALSE);
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return success == TRUE ? 0 : -1;
}

int process_emu___cxa_atexit(Process* proc, void (*func) (void *), void * arg, void * dso_handle) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    gboolean success = FALSE;
    if(dso_handle) {
        /* this should be called when the plugin is unloaded */
        warning("atexit at library close is not currently supported");
    } else {
        Process* proc = worker_getActiveProcess();
        if(proc) {
            success = process_addAtExitCallback(proc, func, arg, TRUE);
        }
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return success == TRUE ? 0 : -1;
}

pid_t process_emu_getpid(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    pid_t pid;

    if(prevCTX == PCTX_PLUGIN) {
        /* POSIX specifies that all threads return the process id */
        pid = (pid_t)proc->processID;
    } else {
        pid = getpid();
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return pid;
}

pid_t process_emu_getppid(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    pid_t pid;

    if(prevCTX == PCTX_PLUGIN) {
        /* we list a constant as the parent process */
        pid = 0;
    } else {
        /* if shadow is calling this, get shadow's real ppid */
        pid = getppid();
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return pid;
}

/* syscall */

int process_emu_syscall(Process* proc, int number, va_list ap) {
	va_list args;
	va_copy(args, ap);

    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);

    int do_syscall = 0;
    int result = 0;
    int ret = 0;

    switch (number) {
#if defined SYS_clock_gettime
		case SYS_clock_gettime: {
			/* get the args for clock_gettime */
			clockid_t id = va_arg(args, clockid_t);
			struct timespec* ts = va_arg(args, struct timespec*);

			/* call our emulation version, which thinks its being called from a non-shadow context */
			_process_changeContext(proc, PCTX_SHADOW, prevCTX);
			result = process_emu_clock_gettime(proc, id, ts);
			_process_changeContext(proc, prevCTX, PCTX_SHADOW);

			/* result is our actual return value.
			 * if result is -1, then the process errno was already set */
			ret = result;
			break;
		}
#endif

#if defined SYS_getrandom
		case SYS_getrandom: {
			uint8_t* out = va_arg(args, uint8_t*);
			size_t out_len = va_arg(args, size_t);
			const unsigned int flags = va_arg(args, const unsigned int);

			/* get the random bytes internally from Shadow's random source
			 * for this host to maintain determistic behavior */
			random_nextNBytes(host_getRandom(proc->host), (guchar*)out, out_len);

			if(out_len > INT_MAX) {
				ret = INT_MAX;
			} else {
				ret = (int)out_len;
			}

			break;
		}
#endif

#if defined SYS_gettid
        /* thread ids need to be unique for every thread, and unique from the pid */
        case SYS_gettid: {
            pth_t thread = pth_self();
            if (thread == proc->shadowThread) {
                ret = (int) getpid();
            } else if (thread == proc->programMainThread) {
                ret = (int) proc->processID;
            } else {
                gpointer val = g_hash_table_lookup(proc->programAuxThreads, thread);
                utility_assert(val);
                guint tid = GPOINTER_TO_UINT(val);
                ret = (int)tid;
            }

            break;
        }
#endif

		/* TODO the following are functions that shadow normally intercepts, and we should handle them */

#if defined SYS_accept
        case SYS_accept:
#endif
#if defined SYS_accept4
        case SYS_accept4:
#endif
#if defined SYS_bind
        case SYS_bind:
#endif
#if defined SYS_close
        case SYS_close:
#endif
#if defined SYS_connect
        case SYS_connect:
#endif
#if defined SYS_creat
        case SYS_creat:
#endif
#if defined SYS_dup
        case SYS_dup:
#endif
#if defined SYS_dup2
        case SYS_dup2:
#endif
#if defined SYS_dup3
        case SYS_dup3:
#endif
#if defined SYS_epoll_create
        case SYS_epoll_create:
#endif
#if defined SYS_epoll_create1
        case SYS_epoll_create1:
#endif
#if defined SYS_epoll_ctl
        case SYS_epoll_ctl:
#endif
#if defined SYS_epoll_pwait
        case SYS_epoll_pwait:
#endif
#if defined SYS_epoll_wait
        case SYS_epoll_wait:
#endif
#if defined SYS_eventfd
        case SYS_eventfd:
#endif
#if defined SYS_exit
        case SYS_exit:
#endif
#if defined SYS_faccessat
        case SYS_faccessat:
#endif
#if defined SYS_fallocate
        case SYS_fallocate:
#endif
#if defined SYS_fchdir
        case SYS_fchdir:
#endif
#if defined SYS_fchmod
        case SYS_fchmod:
#endif
#if defined SYS_fchmodat
        case SYS_fchmodat:
#endif
#if defined SYS_fchown
        case SYS_fchown:
#endif
#if defined SYS_fchownat
        case SYS_fchownat:
#endif
#if defined SYS_fcntl
        case SYS_fcntl:
#endif
#if defined SYS_fdatasync
        case SYS_fdatasync:
#endif
#if defined SYS_flock
        case SYS_flock:
#endif
#if defined SYS_fork
        case SYS_fork:
#endif
#if defined SYS_fstat
        case SYS_fstat:
#endif
#if defined SYS_fstatfs
        case SYS_fstatfs:
#endif
#if defined SYS_fstatfs64
        case SYS_fstatfs64:
#endif
#if defined SYS_fsync
        case SYS_fsync:
#endif
#if defined SYS_ftruncate
        case SYS_ftruncate:
#endif
#if defined SYS_ftruncate64
        case SYS_ftruncate64:
#endif
#if defined SYS_gethostname
        case SYS_gethostname:
#endif
#if defined SYS_getpeername
        case SYS_getpeername:
#endif
#if defined SYS_getsockname
        case SYS_getsockname:
#endif
#if defined SYS_getsockopt
        case SYS_getsockopt:
#endif
#if defined SYS_gettimeofday
        case SYS_gettimeofday:
#endif
#if defined SYS_ioctl
        case SYS_ioctl:
#endif
#if defined SYS_listen
        case SYS_listen:
#endif
#if defined SYS_lock
        case SYS_lock:
#endif
#if defined SYS_lseek
        case SYS_lseek:
#endif
#if defined SYS_mmap
        case SYS_mmap:
#endif
#if defined SYS_nanosleep
        case SYS_nanosleep:
#endif
#if defined SYS_open
        case SYS_open:
#endif
#if defined SYS_openat
        case SYS_openat:
#endif
#if defined SYS_pipe
        case SYS_pipe:
#endif
#if defined SYS_pipe2
        case SYS_pipe2:
#endif
#if defined SYS_poll
        case SYS_poll:
#endif
#if defined SYS_ppoll
        case SYS_ppoll:
#endif
#if defined SYS_read
        case SYS_read:
#endif
#if defined SYS_readv
        case SYS_readv:
#endif
#if defined SYS_recv
        case SYS_recv:
#endif
#if defined SYS_recvfrom
        case SYS_recvfrom:
#endif
#if defined SYS_recvmsg
        case SYS_recvmsg:
#endif
#if defined SYS_select
        case SYS_select:
#endif
#if defined SYS_send
        case SYS_send:
#endif
#if defined SYS_sendmsg
        case SYS_sendmsg:
#endif
#if defined SYS_sendto
        case SYS_sendto:
#endif
#if defined SYS_setsockopt
        case SYS_setsockopt:
#endif
#if defined SYS_shutdown
        case SYS_shutdown:
#endif
#if defined SYS_sigaction
        case SYS_sigaction:
#endif
#if defined SYS_socket
        case SYS_socket:
#endif
#if defined SYS_socketpair
        case SYS_socketpair:
#endif
#if defined SYS_sync
        case SYS_sync:
#endif
#if defined SYS_syncfs
        case SYS_syncfs:
#endif
#if defined SYS_syscall
        case SYS_syscall:
#endif
#if defined SYS_time
        case SYS_time:
#endif
#if defined SYS_timerfd
        case SYS_timerfd:
#endif
#if defined SYS_timerfd_create
        case SYS_timerfd_create:
#endif
#if defined SYS_timerfd_gettime
        case SYS_timerfd_gettime:
#endif
#if defined SYS_timerfd_settime
        case SYS_timerfd_settime:
#endif
#if defined SYS_unlink
        case SYS_unlink:
#endif
#if defined SYS_unlinkat
        case SYS_unlinkat:
#endif
#if defined SYS_waitpid
        case SYS_waitpid:
#endif
#if defined SYS_write
        case SYS_write:
#endif
#if defined SYS_writev
        case SYS_writev:
#endif

		{
			/* Shadow should deal with these, so this is a critical issue that we should address */
			error("syscall() was called with syscall number '%i'. Shadow handles the libc version of this "
					"function, but does not yet handle the syscall() version, and therefore "
					"this function call is unlikely to work correctly because it is not Shadow-aware. "
					"Please report this error at https://github.com/shadow/shadow/issues.", number);
			do_syscall = 0;
			break;
		}

		default: {
			/* We may get by with letting the kernel deal with this since it may not affect Shadow. */
			info("syscall() was called with number '%i'. Shadow does not yet intercept this function. "
					"We will forward to the kernel/libc, which is not Shadow-aware and is not guaranteed "
					"to handle things correctly. "
					"Please report if you notice strange behavior.", number);
			do_syscall = 1;
			break;
		}
    }

    if(do_syscall) {
    	result = syscall(number, ap);
    	if(result == EOF) {
			_process_setErrno(proc, errno);
		}
    	ret = result;
    }

    _process_changeContext(proc, PCTX_SHADOW, prevCTX);

    va_end(args);
    return ret;
}

/* pthread attributes */

int process_emu_pthread_attr_init(Process* proc, pthread_attr_t *attr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        pth_attr_t na = NULL;
        if (attr == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else if ((na = pth_attr_new()) == NULL) {
            ret = errno;
        } else {
            memmove(attr, &na, sizeof(void*));
            ret = 0;
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_init() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_destroy(Process* proc, pthread_attr_t *attr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (attr == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                int result = pth_attr_destroy(na);
                memset(attr, 0, sizeof(void*));
                if(result == -1) {
                    _process_setErrno(proc, errno);
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_destroy() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_getattr_np(Process* proc, pthread_t thread, pthread_attr_t *attr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        pth_t pt = NULL;
        memmove(&pt, &thread, sizeof(void*));
        if(pt == NULL || attr == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            utility_assert(proc->tstate == pth_gctx_get());
            pth_attr_t na = NULL;
            if(pth_getattr_np(pt, (pth_attr_t) attr)) {
                ret = errno;
            }
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        }
    } else {
        warning("pthread_getattr_np() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setinheritsched(Process* proc, pthread_attr_t *attr, int inheritsched) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_setinheritsched() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getinheritsched(Process* proc, const pthread_attr_t *attr, int *inheritsched) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL || inheritsched == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_getinheritsched() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setschedparam(Process* proc, pthread_attr_t *attr,
        const struct sched_param *schedparam) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_setschedparam() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getschedparam(Process* proc, const pthread_attr_t *attr,
        struct sched_param *schedparam) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL || schedparam == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_getschedparam() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setschedpolicy(Process* proc, pthread_attr_t *attr, int schedpolicy) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_setschedpolicy() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getschedpolicy(Process* proc, const pthread_attr_t *attr, int *schedpolicy) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL || schedpolicy == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_getschedpolicy() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setscope(Process* proc, pthread_attr_t *attr, int scope) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_setscope() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getscope(Process* proc, const pthread_attr_t *attr, int *scope) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(attr == NULL || scope == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_getscope() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setstacksize(Process* proc, pthread_attr_t *attr, size_t stacksize) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if(attr == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if(!pth_attr_set(na, PTH_ATTR_STACK_SIZE, (unsigned int) stacksize)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_setstacksize() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getstacksize(Process* proc, const pthread_attr_t *attr, size_t *stacksize) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (attr == NULL || stacksize == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if (!pth_attr_get(na, PTH_ATTR_STACK_SIZE, (unsigned int *) stacksize)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_getstacksize() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setstackaddr(Process* proc, pthread_attr_t *attr, void *stackaddr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (attr == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if(!pth_attr_set(na, PTH_ATTR_STACK_ADDR, (char *) stackaddr)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_setstackaddr() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getstackaddr(Process* proc, const pthread_attr_t *attr, void **stackaddr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (attr == NULL || stackaddr == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if(!pth_attr_get(na, PTH_ATTR_STACK_ADDR, (char **) stackaddr)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_getstackaddr() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setdetachstate(Process* proc, pthread_attr_t *attr, int detachstate) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if(attr == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if (detachstate == PTHREAD_CREATE_DETACHED) {
                    if (!pth_attr_set(na, PTH_ATTR_JOINABLE, FALSE)) {
                        ret = errno;
                    } else {
                        ret = 0;
                    }
                } else if (detachstate == PTHREAD_CREATE_JOINABLE) {
                    if (!pth_attr_set(na, PTH_ATTR_JOINABLE, TRUE)) {
                        ret = errno;
                    } else {
                        ret = 0;
                    }
                } else {
                    ret = EINVAL;
                    _process_setErrno(proc, EINVAL);
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_setdetachstate() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getdetachstate(Process* proc, const pthread_attr_t *attr, int *detachstate) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if(attr == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            int s = 0;
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if (!pth_attr_get(na, PTH_ATTR_JOINABLE, &s)) {
                    ret = errno;
                } else {
                    ret = 0;
                    if (s == TRUE) {
                        *detachstate = PTHREAD_CREATE_JOINABLE;
                    } else {
                        *detachstate = PTHREAD_CREATE_DETACHED;
                    }
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_getdetachstate() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setguardsize(Process* proc, pthread_attr_t *attr, size_t stacksize) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_setguardsize() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getguardsize(Process* proc, const pthread_attr_t *attr, size_t *stacksize) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL || stacksize == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_attr_setguardsize() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setname_np(Process* proc, pthread_attr_t *attr, char *name) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if(attr == NULL || name == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if (!pth_attr_set(na, PTH_ATTR_NAME, name)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_setname_np() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getname_np(Process* proc, const pthread_attr_t *attr, char **name) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if(attr == NULL || name == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if(!pth_attr_get(na, PTH_ATTR_NAME, name)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_setname_np() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_setprio_np(Process* proc, pthread_attr_t *attr, int prio) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (attr == NULL || (prio < PTH_PRIO_MIN || prio > PTH_PRIO_MAX)) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if (!pth_attr_set(na, PTH_ATTR_PRIO, prio)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_setprio_np() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_attr_getprio_np(Process* proc, const pthread_attr_t *attr, int *prio) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (attr == NULL || prio == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            pth_attr_t na = NULL;
            memmove(&na, attr, sizeof(void*));
            if(na == NULL) {
                ret = EINVAL;
                _process_setErrno(proc, EINVAL);
            } else {
                if (!pth_attr_get(na, PTH_ATTR_PRIO, prio)) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_attr_getprio_np() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread threads */

int process_emu_pthread_create(Process* proc, pthread_t *thread, const pthread_attr_t *attr,
        void *(*start_routine)(void *), void *arg) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (thread == NULL || start_routine == NULL) {
            ret = EINVAL;
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
            _process_setErrno(proc, EINVAL);
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        } else if (pth_ctrl(PTH_CTRL_GETTHREADS) >= 10000) { // arbitrary limit
            ret = EAGAIN;
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
            _process_setErrno(proc, EAGAIN);
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        } else {
            pth_t auxThread = NULL;
            process_ref(proc);
            ProcessChildData* data = g_new0(ProcessChildData, 1);
            data->proc = proc;
            data->run = start_routine;
            data->arg = arg;

            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
            guint threadID = host_getNewProcessID(proc->host);
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

            if (attr != NULL) {
                pth_attr_t customAttr = NULL;
                memmove(&customAttr, attr, sizeof(void*));
                auxThread = pth_spawn(customAttr, (PthSpawnFunc) _process_executeChild, data);
            } else {
                /* default for new auxiliary threads */
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                GString* programAuxThreadNameBuf = g_string_new(NULL);
                g_string_printf(programAuxThreadNameBuf, "%s.%s.%u.aux%u", host_getName(proc->host),
                        _process_getPluginName(proc), proc->processID, threadID);
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);

                pth_attr_t defaultAttr = pth_attr_new();
                pth_attr_set(defaultAttr, PTH_ATTR_NAME, programAuxThreadNameBuf->str);
                pth_attr_set(defaultAttr, PTH_ATTR_STACK_SIZE, PROC_PTH_STACK_SIZE);
                pth_attr_set(defaultAttr, PTH_ATTR_JOINABLE, TRUE);

                auxThread = pth_spawn(defaultAttr, (PthSpawnFunc) _process_executeChild, data);

                /* cleanup */
                pth_attr_destroy(defaultAttr);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                g_string_free(programAuxThreadNameBuf, TRUE);
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            }

            if(auxThread == NULL) {
                g_free(data);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                process_unref(proc);
                ret = EAGAIN;
                _process_setErrno(proc, EAGAIN);
            } else {
                memmove(thread, &auxThread, sizeof(void*));
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                g_hash_table_insert(proc->programAuxThreads, auxThread, GUINT_TO_POINTER(threadID));
                ret = 0;
            }
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_create() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = -1;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_detach(Process* proc, pthread_t thread) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        pth_t pt = NULL;
        memmove(&pt, &thread, sizeof(void*));
        if(pt == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            utility_assert(proc->tstate == pth_gctx_get());
            pth_attr_t na = NULL;
            if((na = pth_attr_of(pt)) == NULL) {
                ret = errno;
            } else if(!pth_attr_set(na, PTH_ATTR_JOINABLE, FALSE)) {
                ret = errno;
            } else {
                pth_attr_destroy(na);
                ret = 0;
            }
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        }
    } else {
        warning("pthread_detach() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu___pthread_detach(Process* proc, pthread_t thread) {
    return process_emu_pthread_detach(proc, thread);
}

pthread_t process_emu_pthread_self(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    pthread_t ret;
    memset(&ret, 0, sizeof(void*));
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        pth_t pt = pth_self();
        memmove(&ret, &pt, sizeof(void*));

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_self() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_equal(Process* proc, pthread_t t1, pthread_t t2) {
    return (t1 == t2);
}

int process_emu_pthread_yield(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        pth_yield(NULL);
        ret = 0;

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_yield() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_yield_np(Process* proc) {
    return process_emu_pthread_yield(proc);
}

void process_emu_pthread_exit(Process* proc, void *value_ptr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    warning("pthread_exit() was called in process '%s'", _process_getName(proc));
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    _process_exitHelper(proc, value_ptr);
}

int process_emu_pthread_join(Process* proc, pthread_t thread, void **value_ptr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        pth_t pt = NULL;
        memmove(&pt, &thread, sizeof(void*));
        if(pt == NULL) {
            ret = EINVAL;
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
            _process_setErrno(proc, EINVAL);
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        } else {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            utility_assert(proc->tstate == pth_gctx_get());

                if (!pth_join(pt, value_ptr)) {
                    ret = errno;
                } else {
                    g_hash_table_remove(proc->programAuxThreads, pt);
                    if (value_ptr != NULL && *value_ptr == PTH_CANCELED) {
                        *value_ptr = PTHREAD_CANCELED;
                    }
                    ret = 0;
                }
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        }
    } else {
        warning("pthread_join() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_once(Process* proc, pthread_once_t *once_control, void (*init_routine)(void)) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if(once_control == NULL || init_routine == NULL) {
            ret = EINVAL;
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
            _process_setErrno(proc, EINVAL);
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        } else {
            if(*once_control != 1) {
                _process_changeContext(proc, PCTX_PTH, PCTX_PLUGIN);
                init_routine();
                _process_changeContext(proc, PCTX_PLUGIN, PCTX_PTH);
            }
            *once_control = 1;
            ret = 0;
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_once() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_sigmask(Process* proc, int how, const sigset_t *set, sigset_t *oset) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        ret = pth_sigmask(how, set, oset);

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if(ret == -1) {
            _process_setErrno(proc, errno);
        }
    } else {
        warning("pthread_sigmask() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_kill(Process* proc, pthread_t thread, int sig) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        pth_t pt = NULL;
        memmove(&pt, &thread, sizeof(void*));
        if(pt == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            utility_assert(proc->tstate == pth_gctx_get());

            if (!pth_raise(pt, sig)) {
                ret = errno;
            } else {
                ret = 0;
            }

            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        }
    } else {
        warning("pthread_kill() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_abort(Process* proc, pthread_t thread) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        pth_t pt = NULL;
        memmove(&pt, &thread, sizeof(void*));
        if(pt == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            utility_assert(proc->tstate == pth_gctx_get());

            if (!pth_abort(pt)) {
                ret = errno;
            } else {
                g_hash_table_remove(proc->programAuxThreads, pt);
                ret = 0;
            }

            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        }
    } else {
        warning("pthread_abort() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/*
**  CONCURRENCY ROUTINES
**
**  We just have to provide the interface, because SUSv2 says:
**  "The pthread_setconcurrency() function allows an application to
**  inform the threads implementation of its desired concurrency
**  level, new_level. The actual level of concurrency provided by the
**  implementation as a result of this function call is unspecified."
*/

int process_emu_pthread_getconcurrency(Process* proc) {
    return proc->pthread_concurrency;
}

int process_emu_pthread_setconcurrency(Process* proc, int new_level) {
    gint ret = 0;
    if (new_level < 0) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        proc->pthread_concurrency = new_level;
    }
    return ret;
}

/* pthread context */

int process_emu_pthread_key_create(Process* proc, pthread_key_t *key, void (*destructor)(void *)) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (!pth_key_create((pth_key_t *)key, destructor)) {
            ret = errno;
        } else {
            ret = 0;
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_key_create() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_key_delete(Process* proc, pthread_key_t key) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (!pth_key_delete((pth_key_t)key)) {
            ret = errno;
        } else {
            ret = 0;
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_key_delete() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_setspecific(Process* proc, pthread_key_t key, const void *value) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        if (!pth_key_setdata((pth_key_t)key, value)) {
            ret = errno;
        } else {
            ret = 0;
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_setspecific() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

void* process_emu_pthread_getspecific(Process* proc, pthread_key_t key) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    void* ret = NULL;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        ret = pth_key_getdata((pth_key_t)key);

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_getspecific() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = NULL;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread cancel */

int process_emu_pthread_cancel(Process* proc, pthread_t thread) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        pth_t pt = NULL;
        memmove(&pt, &thread, sizeof(void*));
        if(pt == NULL) {
            ret = EINVAL;
            _process_setErrno(proc, EINVAL);
        } else {
            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            utility_assert(proc->tstate == pth_gctx_get());

            if (!pth_cancel(pt)) {
                ret = errno;
            } else {
                ret = 0;
            }

            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
        }
    } else {
        warning("pthread_cancel() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

void process_emu_pthread_testcancel(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        pth_cancel_point();

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_testcancel() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
}

int process_emu_pthread_setcancelstate(Process* proc, int state, int *oldstate) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        int s, os;

        if (oldstate != NULL) {
            pth_cancel_state(0, &os);
            if (os & PTH_CANCEL_ENABLE) {
                *oldstate = PTHREAD_CANCEL_ENABLE;
            } else {
                *oldstate = PTHREAD_CANCEL_DISABLE;
            }
        }
        if (state != 0) {
            pth_cancel_state(0, &s);
            if (state == PTHREAD_CANCEL_ENABLE) {
                s |= PTH_CANCEL_ENABLE;
                s &= ~(PTH_CANCEL_DISABLE);
            }
            else {
                s |= PTH_CANCEL_DISABLE;
                s &= ~(PTH_CANCEL_ENABLE);
            }
            pth_cancel_state(s, NULL);
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_setcancelstate() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_setcanceltype(Process* proc, int type, int *oldtype) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        int t, ot;

        if (oldtype != NULL) {
            pth_cancel_state(0, &ot);
            if (ot & PTH_CANCEL_DEFERRED) {
                *oldtype = PTHREAD_CANCEL_DEFERRED;
            } else {
                *oldtype = PTHREAD_CANCEL_ASYNCHRONOUS;
            }
        }
        if (type != 0) {
            pth_cancel_state(0, &t);
            if (type == PTHREAD_CANCEL_DEFERRED) {
                t |= PTH_CANCEL_DEFERRED;
                t &= ~(PTH_CANCEL_ASYNCHRONOUS);
            }
            else {
                t |= PTH_CANCEL_ASYNCHRONOUS;
                t &= ~(PTH_CANCEL_DEFERRED);
            }
            pth_cancel_state(t, NULL);
        }

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_setcanceltype() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread scheduler */

int process_emu_pthread_setschedparam(Process* proc, pthread_t pthread, int policy, const struct sched_param *param) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = ENOSYS;
    warning("pthread_setschedparam() is not supported by pth or by shadow");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_getschedparam(Process* proc, pthread_t pthread, int *policy, struct sched_param *param) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = ENOSYS;
    warning("pthread_getschedparam() is not supported by pth or by shadow");
    _process_setErrno(proc, ENOSYS);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread cleanup */

void process_emu_pthread_cleanup_push(Process* proc, void (*routine)(void *), void *arg) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        // FIXME this was causing SEGFAULTs in Tor when the cleanup func was later run
        //pth_cleanup_push(routine, arg);

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_cleanup_push() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
}

void process_emu_pthread_cleanup_pop(Process* proc, int execute) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());

        pth_cleanup_pop(execute);

        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
    } else {
        warning("pthread_cleanup_pop() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
}

/*
**  AT-FORK SUPPORT
*/

int process_emu_pthread_atfork(Process* proc, void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        pth_gctx_t pth_gctx = pth_gctx_get();
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        utility_assert(proc->tstate == pth_gctx);

        if(prepare) {
            ProcessAtForkCallbackData* data = g_new0(ProcessAtForkCallbackData, 1);
            data->prepare = prepare;

            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            int atfork_result = pth_atfork_push((PthAtForkFunc)_process_executeAtFork, NULL, NULL, data);
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

            if (!atfork_result) {
                ret = errno;
                g_free(data);
            } else {
                process_ref(proc);
                data->proc = proc;
            }
        }

        if(parent) {
            ProcessAtForkCallbackData* data = g_new0(ProcessAtForkCallbackData, 1);
            data->parent = parent;

            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            int atfork_result = pth_atfork_push(NULL, (PthAtForkFunc)_process_executeAtFork, NULL, data);
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

            if (!atfork_result) {
                ret = errno;
                g_free(data);
            } else {
                process_ref(proc);
                data->proc = proc;
            }
        }

        if(child) {
            ProcessAtForkCallbackData* data = g_new0(ProcessAtForkCallbackData, 1);
            data->child = child;

            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            int atfork_result = pth_atfork_push(NULL, NULL, (PthAtForkFunc)_process_executeAtFork, data);
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

            if (!atfork_result) {
                ret = errno;
                g_free(data);
            } else {
                process_ref(proc);
                data->proc = proc;
            }
        }
    } else {
        warning("pthread_atfork() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread mutex attributes */

int process_emu_pthread_mutexattr_init(Process* proc, pthread_mutexattr_t *attr) {
    if (attr == NULL) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return EINVAL;
    } else {
        /* nothing to do for us */
        return 0;
    }
}

int process_emu_pthread_mutexattr_destroy(Process* proc, pthread_mutexattr_t *attr) {
    if (attr == NULL) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return EINVAL;
    } else {
        /* nothing to do for us */
        return 0;
    }
}

int process_emu_pthread_mutexattr_setprioceiling(Process* proc, pthread_mutexattr_t *attr, int prioceiling) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_setprioceiling() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutexattr_getprioceiling(Process* proc, const pthread_mutexattr_t *attr, int *prioceiling) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_getprioceiling() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutexattr_setprotocol(Process* proc, pthread_mutexattr_t *attr, int protocol) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_setprotocol() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutexattr_getprotocol(Process* proc, const pthread_mutexattr_t *attr, int *protocol) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_getprotocol() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutexattr_setpshared(Process* proc, pthread_mutexattr_t *attr, int pshared) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_setpshared() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutexattr_getpshared(Process* proc, const pthread_mutexattr_t *attr, int *pshared) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_getpshared() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutexattr_settype(Process* proc, pthread_mutexattr_t *attr, int type) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_settype() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);

        ret = 0;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutexattr_gettype(Process* proc, const pthread_mutexattr_t *attr, int *type) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_gettype() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread mutex */

int process_emu_pthread_mutex_init(Process* proc, pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if(prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (mutex == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_mutex_t* pm = g_malloc(sizeof(pth_mutex_t));

            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            int result = pth_mutex_init(pm);
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

            if (!result) {
                ret = errno;
            } else {
                memmove(mutex, &pm, sizeof(void*));
                ret = 0;
            }
        }
    } else {
        warning("pthread_mutex_init() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutex_destroy(Process* proc, pthread_mutex_t *mutex) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (mutex == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_mutex_t* pm = NULL;
            memmove(&pm, mutex, sizeof(void*));
            if(pm == NULL) {
                _process_setErrno(proc, EINVAL);
                ret = EINVAL;
            } else {
                free(pm);
                memset(mutex, 0, sizeof(void*));
                ret = 0;
            }
        }
    } else {
        warning(
                "pthread_mutex_destroy() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutex_setprioceiling(Process* proc, pthread_mutex_t *mutex, int prioceiling, int *old_ceiling) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (mutex == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_setprioceiling() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutex_getprioceiling(Process* proc, const pthread_mutex_t *mutex, int *prioceiling) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (mutex == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_mutexattr_getprioceiling() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutex_lock(Process* proc, pthread_mutex_t *mutex) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (mutex == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_mutex_t* pm = NULL;
            memmove(&pm, mutex, sizeof(void*));

            int init_result = 0;
            if(pm == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_mutex_init(proc, mutex, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&pm, mutex, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_mutex_acquire(pm, FALSE, NULL);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_mutex_lock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutex_trylock(Process* proc, pthread_mutex_t *mutex) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (mutex == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_mutex_t* pm = NULL;
            memmove(&pm, mutex, sizeof(void*));

            int init_result = 0;
            if(pm == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_mutex_init(proc, mutex, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&pm, mutex, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_mutex_acquire(pm, TRUE, NULL);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_mutex_trylock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_mutex_unlock(Process* proc, pthread_mutex_t *mutex) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (mutex == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_mutex_t* pm = NULL;
            memmove(&pm, mutex, sizeof(void*));

            int init_result = 0;
            if(pm == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_mutex_init(proc, mutex, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&pm, mutex, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_mutex_release(pm);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_mutex_unlock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread lock attributes */

int process_emu_pthread_rwlockattr_init(Process* proc, pthread_rwlockattr_t *attr) {
    if (attr == NULL) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return EINVAL;
    } else {
        /* nothing to do for us */
        return 0;
    }
}

int process_emu_pthread_rwlockattr_destroy(Process* proc, pthread_rwlockattr_t *attr) {
    if (attr == NULL) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return EINVAL;
    } else {
        /* nothing to do for us */
        return 0;
    }
}

int process_emu_pthread_rwlockattr_setpshared(Process* proc, pthread_rwlockattr_t *attr, int pshared) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_rwlockattr_setpshared() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_rwlockattr_getpshared(Process* proc, const pthread_rwlockattr_t *attr, int *pshared) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_rwlockattr_getpshared() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread locks */

int process_emu_pthread_rwlock_init(Process* proc, pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (rwlock == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_rwlock_t *rw;
            if((rw = (pth_rwlock_t *)malloc(sizeof(pth_rwlock_t))) == NULL) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                int result = pth_rwlock_init(rw);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if (!result) {
                    ret = errno;
                } else {
                    memmove(rwlock, &rw, sizeof(void*));
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_rwlock_init() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_rwlock_destroy(Process* proc, pthread_rwlock_t *rwlock) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (rwlock == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_rwlock_t* prw = NULL;
            memmove(&prw, rwlock, sizeof(void*));
            if(prw == NULL) {
                _process_setErrno(proc, EINVAL);
                ret = EINVAL;
            } else {
                free(prw);
                memset(rwlock, 0, sizeof(void*));
                ret = 0;
            }
        }
    } else {
        warning("pthread_rwlock_destroy() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_rwlock_rdlock(Process* proc, pthread_rwlock_t *rwlock) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (rwlock == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_rwlock_t* prw = NULL;
            memmove(&prw, rwlock, sizeof(void*));
            int init_result = 0;
            if(prw == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_rwlock_init(proc, rwlock, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&prw, rwlock, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_rwlock_acquire(prw, PTH_RWLOCK_RD, FALSE, NULL);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }

        }
    } else {
        warning("pthread_rwlock_rdlock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_rwlock_tryrdlock(Process* proc, pthread_rwlock_t *rwlock) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (rwlock == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_rwlock_t* prw = NULL;
            memmove(&prw, rwlock, sizeof(void*));
            int init_result = 0;
            if(prw == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_rwlock_init(proc, rwlock, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&prw, rwlock, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_rwlock_acquire(prw, PTH_RWLOCK_RD, TRUE, NULL);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_rwlock_tryrdlock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_rwlock_wrlock(Process* proc, pthread_rwlock_t *rwlock) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (rwlock == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_rwlock_t* prw = NULL;
            memmove(&prw, rwlock, sizeof(void*));
            int init_result = 0;
            if(prw == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_rwlock_init(proc, rwlock, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&prw, rwlock, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_rwlock_acquire(prw, PTH_RWLOCK_RW, FALSE, NULL);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }

    } else {
        warning("pthread_rwlock_wrlock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_rwlock_trywrlock(Process* proc, pthread_rwlock_t *rwlock) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (rwlock == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_rwlock_t* prw = NULL;
            memmove(&prw, rwlock, sizeof(void*));
            int init_result = 0;
            if(prw == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_rwlock_init(proc, rwlock, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&prw, rwlock, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_rwlock_acquire(prw, PTH_RWLOCK_RW, TRUE, NULL);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_rwlock_trywrlock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_rwlock_unlock(Process* proc, pthread_rwlock_t *rwlock) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (rwlock == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            int init_result = 0;
            pth_rwlock_t* prw = NULL;
            memmove(&prw, rwlock, sizeof(void*));
            if(prw == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_rwlock_init(proc, rwlock, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&prw, rwlock, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_rwlock_release(prw);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_rwlock_unlock() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* pthread condition attributes */

int process_emu_pthread_condattr_init(Process* proc, pthread_condattr_t *attr) {
    if (attr == NULL) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return EINVAL;
    } else {
        /* nothing to do for us */
        return 0;
    }
}

int process_emu_pthread_condattr_destroy(Process* proc, pthread_condattr_t *attr) {
    if (attr == NULL) {
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
        _process_setErrno(proc, EINVAL);
        _process_changeContext(proc, PCTX_SHADOW, prevCTX);
        return EINVAL;
    } else {
        /* nothing to do for us */
        return 0;
    }
}

int process_emu_pthread_condattr_setpshared(Process* proc, pthread_condattr_t *attr, int pshared) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_condattr_setpshared() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_condattr_getpshared(Process* proc, const pthread_condattr_t *attr, int *pshared) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (attr == NULL) {
        ret = EINVAL;
        _process_setErrno(proc, EINVAL);
    } else {
        warning("pthread_condattr_setpshared() is not supported by pth or by shadow");
        ret = ENOSYS;
        _process_setErrno(proc, ENOSYS);
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_condattr_setclock(Process* proc, pthread_condattr_t *attr, clockid_t clock_id) {
    return 0;
}

int process_emu_pthread_condattr_getclock(Process* proc, const pthread_condattr_t *attr, clockid_t* clock_id) {
    return 0;
}

/* pthread conditions */

int process_emu_pthread_cond_init(Process* proc, pthread_cond_t *cond, const pthread_condattr_t *attr) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (cond == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_cond_t *pcn = g_malloc(sizeof(pth_cond_t));

            _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
            int result = pth_cond_init(pcn);
            _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

            if (!result) {
                ret = errno;
            } else {
                memmove(cond, &pcn, sizeof(void*));
                ret = 0;
            }
        }
    } else {
        warning("pthread_cond_init() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_cond_destroy(Process* proc, pthread_cond_t *cond) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (cond == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            pth_cond_t* pcn = NULL;
            memmove(&pcn, cond, sizeof(void*));
            if(pcn == NULL) {
                _process_setErrno(proc, EINVAL);
                ret = EINVAL;
            } else {
                free(pcn);
                memset(cond, 0, sizeof(void*));
                ret = 0;
            }
        }
    } else {
        warning("pthread_cond_destroy() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_cond_broadcast(Process* proc, pthread_cond_t *cond) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (cond == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            int init_result = 0;
            pth_cond_t* pcn = NULL;
            memmove(&pcn, cond, sizeof(void*));
            if(pcn == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_cond_init(proc, cond, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&pcn, cond, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_cond_notify(pcn, TRUE);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_cond_broadcast() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_cond_signal(Process* proc, pthread_cond_t *cond) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (cond == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            int init_result = 0;
            pth_cond_t* pcn = NULL;
            memmove(&pcn, cond, sizeof(void*));
            if(pcn == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_cond_init(proc, cond, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&pcn, cond, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                init_result = pth_cond_notify(pcn, FALSE);
                _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                if(!init_result) {
                    ret = errno;
                } else {
                    ret = 0;
                }
            }
        }
    } else {
        warning("pthread_cond_signal() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_cond_wait(Process* proc, pthread_cond_t *cond, pthread_mutex_t *mutex) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (cond == NULL || mutex == NULL) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            int init_result = 0;
            pth_cond_t* pcn = NULL;
            memmove(&pcn, cond, sizeof(void*));
            if(pcn == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_cond_init(proc, cond, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&pcn, cond, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                init_result = 0;
                pth_mutex_t* pm = NULL;
                memmove(&pm, mutex, sizeof(void*));
                if(pm == NULL) {
                    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                    init_result = process_emu_pthread_mutex_init(proc, mutex, NULL);
                    _process_changeContext(proc, prevCTX, PCTX_SHADOW);
                }
                memmove(&pm, mutex, sizeof(void*));
                if(init_result != 0) {
                    ret = errno;
                } else {
                    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                    init_result = pth_cond_await(pcn, pm, NULL);
                    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                    if(!init_result) {
                        ret = errno;
                    } else {
                        ret = 0;
                    }
                }
            }
        }
    } else {
        warning("pthread_cond_signal() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

int process_emu_pthread_cond_timedwait(Process* proc, pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = 0;
    if (prevCTX == PCTX_PLUGIN) {
        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
        utility_assert(proc->tstate == pth_gctx_get());
        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

        if (cond == NULL || mutex == NULL || abstime == NULL || abstime->tv_sec < 0 || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000) {
            _process_setErrno(proc, EINVAL);
            ret = EINVAL;
        } else {
            int init_result = 0;
            pth_cond_t* pcn = NULL;
            memmove(&pcn, cond, sizeof(void*));
            if(pcn == NULL) {
                _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                init_result = process_emu_pthread_cond_init(proc, cond, NULL);
                _process_changeContext(proc, prevCTX, PCTX_SHADOW);
            }
            memmove(&pcn, cond, sizeof(void*));
            if(init_result != 0) {
                ret = errno;
            } else {
                pth_event_t ev;
                init_result = 0;
                pth_mutex_t* pm = NULL;
                memmove(&pm, mutex, sizeof(void*));
                if(pm == NULL) {
                    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
                    init_result = process_emu_pthread_mutex_init(proc, mutex, NULL);
                    _process_changeContext(proc, prevCTX, PCTX_SHADOW);
                }
                memmove(&pm, mutex, sizeof(void*));
                if(init_result != 0) {
                    ret = errno;
                } else {
                    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                    pth_time_t t = pth_time(abstime->tv_sec, (abstime->tv_nsec)/1000);
                    ev = pth_event(PTH_EVENT_TIME, t);
                    init_result = pth_cond_await(pcn, pm, ev);
                    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);

                    if (!init_result) {
                        ret = errno;
                    } else {
                        _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                        pth_status_t ev_status = pth_event_status(ev);
                        _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                        if (ev_status == PTH_STATUS_OCCURRED) {
                            ret = ETIMEDOUT;
                        } else {
                            ret = 0;
                        }
                    }
                    _process_changeContext(proc, PCTX_SHADOW, PCTX_PTH);
                    pth_event_free(ev, PTH_FREE_THIS);
                    _process_changeContext(proc, PCTX_PTH, PCTX_SHADOW);
                }
            }
        }
    } else {
        warning("pthread_cond_signal() is handled by pth but not implemented by shadow");
        _process_setErrno(proc, ENOSYS);
        ret = ENOSYS;
    }
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}

/* BLEEP related functions*/
// BLEEP Shared Entry Functions
void* process_emu_shadow_claim_shared_entry(Process* proc, void* ptr, size_t sz, int shared_id) {
    void* ret;
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    ret = shadow_claim_shared_entry(ptr, sz, shared_id);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}
void process_emu_shadow_gmutex_lock(Process* proc, int shared_id) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    shadow_gmutex_lock(shared_id);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return;
}
void process_emu_shadow_gmutex_unlock(Process* proc, int shared_id) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    shadow_gmutex_unlock(shared_id);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return;
}
// BLEEP Virtual ID Functions
int process_emu_shadow_assign_virtual_id(Process* proc) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    int ret = shadow_assign_virtual_id();
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return ret;
}
// BLEEP TCP PTR send/recv Functions

// Memory Instrumentation Marker Functions
void process_emu_shadow_instrumentation_marker_set(Process* proc, int file_symbol, int line_cnt) {
    ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW);
    shadow_instrumentation_marker_set(file_symbol, line_cnt);
    _process_changeContext(proc, PCTX_SHADOW, prevCTX);
    return;
}

#define PROCESS_EMU_UNSUPPORTED(returntype, returnval, functionname) \
    returntype process_emu_##functionname(Process* proc, ...) { \
        ProcessContext prevCTX = _process_changeContext(proc, proc->activeContext, PCTX_SHADOW); \
        warning(#functionname " is not supported by pth or by shadow"); \
        _process_setErrno(proc, ENOSYS); \
        _process_changeContext(proc, PCTX_SHADOW, prevCTX); \
        return returnval; \
    }

#include "shd-process-undefined.h"

#undef PROCESS_EMU_UNSUPPORTED

