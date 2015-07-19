/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <pthmt.h>

#include "shadow.h"

typedef void (*PluginExitCallbackFunc)();
typedef void (*PluginExitCallbackArgumentsFunc)(int, void*);

typedef enum _ProcessContext ProcessContext;
enum _ProcessContext {
    PCTX_NONE, PCTX_SHADOW, PCTX_PLUGIN, PCTX_PTH
};

typedef struct _CallbackData CallbackData;
struct _CallbackData {
    gpointer applicationData;
    Process* application;
};

typedef struct _ProcessCallbackData ProcessCallbackData;
struct _ProcessCallbackData {
    CallbackFunc callback;
    gpointer data;
    gpointer argument;
};

typedef struct _ProcessExitCallbackData ProcessExitCallbackData;
struct _ProcessExitCallbackData {
    gpointer callback;
    gpointer argument;
    gboolean passArgument;
};

struct _Process {
    GQuark programID;

    /* the shadow plugin executable */
    Program* prog;
    /* the portable program state this process uses when executing the program */
    ProgramState pstate;
    /* the portable thread state this process uses when executing the program */
    pth_gctx_t* tstate;

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

    /* process boot and shutdown variables */
    SimulationTime startTime;
    GString* arguments;
    GQueue* atExitFunctions;

    gboolean isRunning;
    gint referenceCount;
    MAGIC_DECLARE;
};

Process* process_new(GQuark programID, SimulationTime startTime, SimulationTime stopTime, gchar* arguments) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    proc->programID = programID;
    proc->startTime = startTime;
    proc->arguments = g_string_new(arguments);

    proc->cpuDelayTimer = g_timer_new();
    proc->activeContext = PCTX_SHADOW;
    proc->referenceCount = 1;
    proc->isRunning = TRUE;

    proc->tstate = pth_gctx_new();

    return proc;
}

static void _process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    process_stop(proc);

    g_string_free(proc->arguments, TRUE);

    if(proc->atExitFunctions) {
        g_queue_free_full(proc->atExitFunctions, g_free);
    }

    g_timer_destroy(proc->cpuDelayTimer);

    if(proc->tstate) {
        pth_gctx_free(proc->tstate);
    }

    MAGIC_CLEAR(proc);
    g_free(proc);
}

static void _process_handleTimerResult(Process* proc, gdouble elapsedTimeSec) {
    SimulationTime delay = (SimulationTime) (elapsedTimeSec * SIMTIME_ONE_SECOND);
    Host* currentHost = worker_getCurrentHost();
    cpu_addDelay(host_getCPU(currentHost), delay);
    tracker_addProcessingTime(host_getTracker(currentHost), delay);
}

static void _process_executeMain(Process* proc, gint argcParam, gchar* argvParam[]) {
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));

    PluginMainFunc main = program_getMainFunc(proc->prog);

    g_timer_start(proc->cpuDelayTimer);

    worker_setActiveProcess(proc);
    proc->activeContext = PCTX_PLUGIN;
    main(argcParam, argvParam);
    proc->activeContext = PCTX_SHADOW;
    worker_setActiveProcess(NULL);

    /* no need to call stop */
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);
}

static void _process_executeExitCallback(Process* proc, ProcessExitCallbackData* atexitData) {
    MAGIC_ASSERT(proc);
    utility_assert(atexitData);
    utility_assert(process_isRunning(proc));

    g_timer_start(proc->cpuDelayTimer);

    worker_setActiveProcess(proc);
    proc->activeContext = PCTX_PLUGIN;
    if(atexitData->passArgument) {
        ((PluginExitCallbackArgumentsFunc)atexitData->callback)(0, atexitData->argument);
    } else {
        ((PluginExitCallbackFunc)atexitData->callback)();
    }
    proc->activeContext = PCTX_SHADOW;
    worker_setActiveProcess(NULL);

    /* no need to call stop */
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);
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

static gint _process_getArguments(Process* proc, gchar** argvOut[]) {
    gchar* threadBuffer;

    gchar* argumentString = g_strdup(proc->arguments->str);
    GQueue *arguments = g_queue_new();

    /* first argument is the name of the program */
    const gchar* pluginName = g_quark_to_string(proc->programID);
    g_queue_push_tail(arguments, g_strdup(pluginName));

    /* parse the full argument string into separate strings */
    gchar* token = strtok_r(argumentString, " ", &threadBuffer);
    while(token != NULL) {
        gchar* argument = g_strdup((const gchar*) token);
        g_queue_push_tail(arguments, argument);
        token = strtok_r(NULL, " ", &threadBuffer);
    }

    /* setup for creating new plug-in, i.e. format into argc and argv */
    gint argc = g_queue_get_length(arguments);
    /* a pointer to an array that holds pointers */
    gchar** argv = g_new0(gchar*, argc);

    for(gint i = 0; i < argc; i++) {
        argv[i] = g_queue_pop_head(arguments);
    }

    /* cleanup */
    g_free(argumentString);
    g_queue_free(arguments);

    /* transfer to the caller - they must free argv and each element of it */
    *argvOut = argv;
    return argc;
}

void process_start(Process* proc) {
    MAGIC_ASSERT(proc);

    /* dont do anything if we are already running */
    if(!process_isRunning(proc)) {
        info("starting '%s' process", g_quark_to_string(proc->programID));

        /* need to get thread-private program from current worker */
        proc->prog = worker_getPrivateProgram(proc->programID);

        /* create our default state as we run in our assigned worker */
        proc->pstate = program_newDefaultState(proc->prog);

        // XXX TODO spawn new pth thread

        /* get arguments from the configured software */
        gchar** argv;
        gint argc = _process_getArguments(proc, &argv);

        /* we will need to free each argument, copy argc in case they change it */
        gint n = argc;

        /* now we will execute in the plugin */
        program_swapInState(proc->prog, proc->pstate);
        _process_executeMain(proc, argc, argv);
        program_swapOutState(proc->prog, proc->pstate);

        /* free the arguments */
        for(gint i = 0; i < n; i++) {
            g_free(argv[i]);
        }
        g_free(argv);
    }
}

void process_continue(Process* proc) {
    MAGIC_ASSERT(proc);

    /* only notify if we are running */
    // XXX do something with pth
//    if(process_isRunning(proc)) {
//        program_swapInState(proc->prog, proc->pstate);
//        _process_execute(proc, program_getNotifyFunc(proc->prog));
//        program_swapOutState(proc->prog, proc->pstate);
//    }
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    /* we only have state if we are running */
    if(!process_isRunning(proc)) {
        return;
    }

    info("stopping '%s' process", g_quark_to_string(proc->programID));

    program_swapInState(proc->prog, proc->pstate);

    debug("calling atexit for '%s' process", g_quark_to_string(proc->programID));

    while(proc->atExitFunctions && g_queue_get_length(proc->atExitFunctions) > 0) {
        ProcessExitCallbackData* atexitData = g_queue_pop_head(proc->atExitFunctions);
        _process_executeExitCallback(proc, atexitData);
        g_free(atexitData);
    }

    program_swapOutState(proc->prog, proc->pstate);

    /* free our copy of plug-in resources, and other application state */
    program_freeState(proc->prog, proc->pstate);
    proc->pstate = NULL;
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
    return proc->pstate != NULL ? TRUE : FALSE;
}

gboolean process_shouldInterpose(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));
    return proc->activeContext == PCTX_PLUGIN ? TRUE : FALSE;
}

void process_beginControl(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));
    proc->activeContext = PCTX_SHADOW;
}

void process_endControl(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));
    proc->activeContext = PCTX_PLUGIN;
}
