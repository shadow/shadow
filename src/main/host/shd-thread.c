/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

typedef enum _ThreadContext ThreadContext;
enum _ThreadContext {
    TCTX_NONE, TCTX_SHADOW, TCTX_PLUGIN, TCTX_PTH
};

struct _Thread {
    ThreadContext activeContext;

    Process* parentProcess;
    Program* program;

    GTimer* delayTimer;

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
    gboolean isShadowContext;

    gboolean isRunning;
    gint referenceCount;

    MAGIC_DECLARE;
};

typedef struct _CallbackData CallbackData;
struct _CallbackData {
    gpointer applicationData;
    Process* application;
};

/**************************************************************
 * functions callable by the plugins through shadow's interface
 **************************************************************/

/**
 * This file provides functionality exported to plug-ins. It mostly provides
 * a common interface and re-directs to the appropriate shadow function.
 */

static int _thread_interface_register(PluginNewInstanceFunc new, PluginNotifyFunc free, PluginNotifyFunc notify) {
    Thread* thread = worker_getActiveThread();
    MAGIC_ASSERT(thread);

    thread->activeContext = TCTX_SHADOW;
    program_registerResidentState(thread->program, new, free, notify);
    thread->activeContext = TCTX_PLUGIN;

    return TRUE;
}

static void _thread_interface_log(ShadowLogLevel level, const char* functionName, const char* format, ...) {
    Thread* thread = worker_getActiveThread();
    MAGIC_ASSERT(thread);

    thread->activeContext = TCTX_SHADOW;

    GLogLevelFlags glevel = 0;
    switch(level) {
    case SHADOW_LOG_LEVEL_ERROR:
        glevel = G_LOG_LEVEL_ERROR;
        break;
    case SHADOW_LOG_LEVEL_CRITICAL:
        glevel = G_LOG_LEVEL_CRITICAL;
        break;
    case SHADOW_LOG_LEVEL_WARNING:
        glevel = G_LOG_LEVEL_WARNING;
        break;
    case SHADOW_LOG_LEVEL_MESSAGE:
        glevel = G_LOG_LEVEL_MESSAGE;
        break;
    case SHADOW_LOG_LEVEL_INFO:
        glevel = G_LOG_LEVEL_INFO;
        break;
    case SHADOW_LOG_LEVEL_DEBUG:
        glevel = G_LOG_LEVEL_DEBUG;
        break;
    default:
        glevel = G_LOG_LEVEL_MESSAGE;
        break;
    }

    va_list variableArguments;
    va_start(variableArguments, format);

    const gchar* domain = g_quark_to_string(*program_getID(thread->program));
    logging_logv(domain, glevel, functionName, format, variableArguments);

    va_end(variableArguments);

    thread->activeContext = TCTX_PLUGIN;
}

static void _thread_executeCallbackInPluginContext(gpointer data, ShadowPluginCallbackFunc callback) {
    callback(data);
}

static void _thread_interface_createCallback(ShadowPluginCallbackFunc callback, void* data, uint millisecondsDelay) {
    Thread* thread = worker_getActiveThread();
    MAGIC_ASSERT(thread);

    thread->activeContext = TCTX_SHADOW;

    process_callback(thread->parentProcess,
            (CallbackFunc)_thread_executeCallbackInPluginContext, data, callback, millisecondsDelay);

    thread->activeContext = TCTX_PLUGIN;
}

static int _thread_interface_getBandwidth(in_addr_t ip, uint* bwdown, uint* bwup) {
    if(!bwdown && !bwup) {
        return TRUE;
    }

    gboolean success = FALSE;

    Thread* thread = worker_getActiveThread();
    MAGIC_ASSERT(thread);

    thread->activeContext = TCTX_SHADOW;

    Address* hostAddress = dns_resolveIPToAddress(worker_getDNS(), (guint32)ip);
    if(hostAddress) {
        GQuark id = (GQuark) address_getID(hostAddress);
        if(bwdown) {
            *bwdown = worker_getNodeBandwidthDown(id, ip);
        }
        if(bwup) {
            *bwup = worker_getNodeBandwidthUp(id, ip);
        }
        success = TRUE;
    }

    thread->activeContext = TCTX_PLUGIN;

    return success;
}

/* we send this FunctionTable to each plug-in so it has pointers to our functions.
 * we use this to export shadow functionality to plug-ins. */
ShadowFunctionTable interfaceFunctionTable = {
    &_thread_interface_register,
    &_thread_interface_log,
    &_thread_interface_createCallback,
    &_thread_interface_getBandwidth,
};

/**************************************************************/

Thread* thread_new(Process* parentProc, Program* prog) {
    Thread* thread = g_new0(Thread, 1);
    MAGIC_INIT(thread);

    thread->parentProcess = parentProc;
    thread->program = prog;

    /* timer for CPU delay measurements */
    thread->delayTimer = g_timer_new();

    thread->activeContext = TCTX_SHADOW;
    thread->referenceCount = 1;
    thread->isRunning = TRUE;

    return thread;
}

static void _thread_free(Thread* thread) {
    MAGIC_ASSERT(thread);

    g_timer_destroy(thread->delayTimer);

    MAGIC_CLEAR(thread);
    g_free(thread);
}

void thread_ref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)++;
}

void thread_unref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)--;
    utility_assert(thread->referenceCount >= 0);
    if(thread->referenceCount == 0) {
        _thread_free(thread);
    }
}

static void _thread_handleTimerResult(Thread* thread, gdouble elapsedTimeSec) {
    SimulationTime delay = (SimulationTime) (elapsedTimeSec * SIMTIME_ONE_SECOND);
    Host* currentHost = worker_getCurrentHost();
    cpu_addDelay(host_getCPU(currentHost), delay);
    tracker_addProcessingTime(host_getTracker(currentHost), delay);
}

gboolean thread_isRunning(Thread* thread) {
    MAGIC_ASSERT(thread);
    return thread->isRunning;
}

void thread_stop(Thread* thread) {
    MAGIC_ASSERT(thread);
    thread->isRunning = FALSE;
}

void thread_execute(Thread* thread, PluginNotifyFunc func) {
    MAGIC_ASSERT(thread);
    utility_assert(func);
    utility_assert(thread_isRunning(thread));

    g_timer_start(thread->delayTimer);

    worker_setActiveThread(thread);
    thread->activeContext = TCTX_PLUGIN;
    func();
    thread->activeContext = TCTX_SHADOW;
    worker_setActiveThread(NULL);

    /* no need to call stop */
    gdouble elapsed = g_timer_elapsed(thread->delayTimer, NULL);
    _thread_handleTimerResult(thread, elapsed);
}

void thread_executeNew(Thread* thread, PluginNewInstanceFunc new, gint argcParam, gchar* argvParam[]) {
    MAGIC_ASSERT(thread);
    utility_assert(new);
    utility_assert(thread_isRunning(thread));

    g_timer_start(thread->delayTimer);

    worker_setActiveThread(thread);
    thread->activeContext = TCTX_PLUGIN;
    new(argcParam, argvParam);
    thread->activeContext = TCTX_SHADOW;
    worker_setActiveThread(NULL);

    /* no need to call stop */
    gdouble elapsed = g_timer_elapsed(thread->delayTimer, NULL);
    _thread_handleTimerResult(thread, elapsed);
}

void thread_executeInit(Thread* thread, ShadowPluginInitializeFunc init) {
    MAGIC_ASSERT(thread);
    utility_assert(init);
    utility_assert(thread_isRunning(thread));

    g_timer_start(thread->delayTimer);

    worker_setActiveThread(thread);
    thread->activeContext = TCTX_PLUGIN;
    init(&interfaceFunctionTable);
    thread->activeContext = TCTX_SHADOW;
    worker_setActiveThread(NULL);

    /* no need to call stop */
    gdouble elapsed = g_timer_elapsed(thread->delayTimer, NULL);
    _thread_handleTimerResult(thread, elapsed);
}

void thread_executeCallback2(Thread* thread, CallbackFunc callback, gpointer data, gpointer callbackArgument) {
    MAGIC_ASSERT(thread);
    utility_assert(callback);
    utility_assert(thread_isRunning(thread));

    g_timer_start(thread->delayTimer);

    worker_setActiveThread(thread);
    thread->activeContext = TCTX_PLUGIN;
    callback(data, callbackArgument);
    thread->activeContext = TCTX_SHADOW;
    worker_setActiveThread(NULL);

    gdouble elapsed = g_timer_elapsed(thread->delayTimer, NULL);
    _thread_handleTimerResult(thread, elapsed);
}

void thread_executeExitCallback(Thread* thread, void (*callback)(int , void *), gpointer argument) {
    MAGIC_ASSERT(thread);
    utility_assert(callback);
    utility_assert(thread_isRunning(thread));

    g_timer_start(thread->delayTimer);

    worker_setActiveThread(thread);
    thread->activeContext = TCTX_PLUGIN;
    callback(0, argument);
    thread->activeContext = TCTX_SHADOW;
    worker_setActiveThread(NULL);

    gdouble elapsed = g_timer_elapsed(thread->delayTimer, NULL);
    _thread_handleTimerResult(thread, elapsed);
}

gboolean thread_shouldInterpose(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread_isRunning(thread));
    return thread->activeContext == TCTX_PLUGIN ? TRUE : FALSE;
}

void thread_beginControl(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread_isRunning(thread));
    thread->activeContext = TCTX_SHADOW;
}

void thread_endControl(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread_isRunning(thread));
    thread->activeContext = TCTX_PLUGIN;
}

Process* thread_getParentProcess(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread_isRunning(thread));
    return thread->parentProcess;
}

Program* thread_getProgram(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread_isRunning(thread));
    return thread->program;
}
