/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadow.h"

struct _Process {
    GQuark programID;

    Program* prog;
    ProgramState state;
    Thread* mainThread;

    SimulationTime startTime;
    GString* arguments;

    GQueue* atExitFunctions;
    MAGIC_DECLARE;
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

Process* process_new(GQuark programID, SimulationTime startTime, SimulationTime stopTime, gchar* arguments) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    proc->programID = programID;
    proc->startTime = startTime;
    proc->arguments = g_string_new(arguments);

    return proc;
}

void process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    process_stop(proc);

    g_string_free(proc->arguments, TRUE);

    if(proc->atExitFunctions) {
        g_queue_free_full(proc->atExitFunctions, g_free);
    }

    MAGIC_CLEAR(proc);
    g_free(proc);
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

gboolean process_isRunning(Process* proc) {
    MAGIC_ASSERT(proc);
    return proc->state != NULL ? TRUE : FALSE;
}

void process_start(Process* proc) {
    MAGIC_ASSERT(proc);

    /* dont do anything if we are already running */
    if(!process_isRunning(proc)) {
        info("starting '%s' process", g_quark_to_string(proc->programID));

        /* need to get thread-private program from current worker */
        proc->prog = worker_getPrivateProgram(proc->programID);
        proc->mainThread = thread_new(proc, proc->prog);

        /* make sure the plugin registered before getting our program state */
        if(!program_isRegistered(proc->prog)) {
//            program_swapInState(proc->prog, proc->state);
            thread_executeInit(proc->mainThread, program_getInitFunc(proc->prog));
//            program_swapOutState(proc->prog, proc->state);

            if(!program_isRegistered(proc->prog)) {
                error("The plug-in '%s' must call shadowlib_register()", program_getName(proc->prog));
            }
        }

        /* create our default state as we run in our assigned worker */
        proc->state = program_newDefaultState(proc->prog);

        /* get arguments from the configured software */
        gchar** argv;
        gint argc = _process_getArguments(proc, &argv);

        /* we will need to free each argument, copy argc in case they change it */
        gint n = argc;

        /* now we will execute in the plugin */
        program_swapInState(proc->prog, proc->state);
        thread_executeNew(proc->mainThread, program_getNewFunc(proc->prog), argc, argv);
        program_swapOutState(proc->prog, proc->state);

        /* free the arguments */
        for(gint i = 0; i < n; i++) {
            g_free(argv[i]);
        }
        g_free(argv);
    }
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    /* we only have state if we are running */
    if(process_isRunning(proc)) {
        info("stopping '%s' process", g_quark_to_string(proc->programID));
        program_swapInState(proc->prog, proc->state);

        thread_execute(proc->mainThread, program_getFreeFunc(proc->prog));

        debug("calling atexit for '%s' process", g_quark_to_string(proc->programID));

        while(proc->atExitFunctions && g_queue_get_length(proc->atExitFunctions) > 0) {
            ProcessExitCallbackData* exitCallback = g_queue_pop_head(proc->atExitFunctions);
            if(exitCallback->passArgument) {
                thread_executeExitCallback(proc->mainThread, exitCallback->callback, exitCallback->argument);
            } else {
                thread_execute(proc->mainThread, (PluginNotifyFunc)exitCallback->callback);
            }
            g_free(exitCallback);
        }

        program_swapOutState(proc->prog, proc->state);

        /* free our copy of plug-in resources, and other application state */
        program_freeState(proc->prog, proc->state);
        proc->state = NULL;

        thread_stop(proc->mainThread);
        thread_unref(proc->mainThread);
        proc->mainThread = NULL;
    }
}

void process_notify(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);

    /* only notify if we are running */
    if(process_isRunning(proc)) {
        program_swapInState(proc->prog, proc->state);
        thread_execute(thread, program_getNotifyFunc(proc->prog));
        program_swapOutState(proc->prog, proc->state);
    }
}

static void _process_callbackTimerExpired(Process* proc, ProcessCallbackData* data) {
    MAGIC_ASSERT(proc);
    utility_assert(data);

    if(process_isRunning(proc)) {
        program_swapInState(proc->prog, proc->state);
        thread_executeCallback2(proc->mainThread, data->callback, data->data, data->argument);
        program_swapOutState(proc->prog, proc->state);
    }

    g_free(data);
}

void process_callback(Process* proc, CallbackFunc userCallback,
        gpointer userData, gpointer userArgument, guint millisecondsDelay) {
    MAGIC_ASSERT(proc);
    utility_assert(process_isRunning(proc));

    /* the application wants a callback. since we need it to happen in our
     * application and plug-in context, we create a callback to our own
     * function first, and then redirect and execute theirs
     */

    ProcessCallbackData* data = g_new0(ProcessCallbackData, 1);
    data->callback = userCallback;
    data->data = userData;
    data->argument = userArgument;

    CallbackEvent* event = callback_new((CallbackFunc)_process_callbackTimerExpired, proc, data);
    SimulationTime nanos = SIMTIME_ONE_MILLISECOND * millisecondsDelay;

    /* callback to our own node */
    worker_scheduleEvent((Event*)event, nanos, 0);
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
