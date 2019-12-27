/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/host/process.h"

#include <bits/stdint-intn.h>
#include <bits/stdint-uintn.h>
#include <bits/types/clockid_t.h>
#include <bits/types/struct_timespec.h>
#include <bits/types/struct_timeval.h>
#include <bits/types/struct_tm.h>
#include <bits/types/time_t.h>
#include <fcntl.h>
#include <features.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <ifaddrs.h>
#include <limits.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/un.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>


#include "glib/gprintf.h"
#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/shd-thread-controller.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timer.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _Process {
    /* the handler of system calls made by the process */
    SystemCallHandler* sys;

    /* unique id of the program that this process should run */
    guint processID;
    GString* processName;

    /* the shadow plugin executable */
    struct {
        /* the name and path to the executable that we will exec */
        GString* exeName;
        GString* exePath;

        /* the name and path to a library that we will LD_PRELOAD before exec */
        GString* preloadName;
        GString* preloadPath;

        /* TRUE from when we've called into plug-in code until the call completes.
         * Note that the plug-in may get back into shadow code during execution, by
         * calling a function that we intercept. */
        gboolean isExecuting;
    } plugin;

    /* timer that tracks the amount of CPU time we spend on plugin execution and processing */
    GTimer* cpuDelayTimer;
    gdouble totalRunTime;

    /* process boot and shutdown variables */
    SimulationTime startTime;
    SimulationTime stopTime;
    GString* arguments;
    gchar** argv;
    gint argc;
    gint returnCode;
    gboolean didLogReturnCode;

    /* manages interactions with the forked process and its threads */
    ThreadControlBlock* tcb;

    gint referenceCount;
    MAGIC_DECLARE;
};

static const gchar* _process_getName(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(proc->processName->str);
    return proc->processName->str;
}

static void _process_handleTimerResult(Process* proc, gdouble elapsedTimeSec) {
    SimulationTime delay = (SimulationTime) (elapsedTimeSec * SIMTIME_ONE_SECOND);
    Host* currentHost = worker_getActiveHost();
    cpu_addDelay(host_getCPU(currentHost), delay);
    tracker_addProcessingTime(host_getTracker(currentHost), delay);
    proc->totalRunTime += elapsedTimeSec;
}

static void _process_logReturnCode(Process* proc, gint code) {
    if(!proc->didLogReturnCode) {
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

        proc->didLogReturnCode = TRUE;
    }
}

static void _process_check(Process* proc) {
    MAGIC_ASSERT(proc);

    if(!proc->tcb) {
        return;
    }

    if(threadcontroller_isAlive(proc->tcb)) {
        info("process '%s' is running, but threads are blocked waiting for events", _process_getName(proc));
    } else {
        /* collect return code */
        int returnCode = threadcontroller_stop(proc->tcb);

        message("process '%s' has completed or is otherwise no longer running", _process_getName(proc));
        _process_logReturnCode(proc, returnCode);

        threadcontroller_unref(proc->tcb);
        proc->tcb = NULL;

        message("total runtime for process '%s' was %f seconds", _process_getName(proc), proc->totalRunTime);
    }
}

static void _process_start(Process* proc) {
    MAGIC_ASSERT(proc);

    /* dont do anything if we are already running */
    if(process_isRunning(proc)) {
        return;
    }

    utility_assert(proc->tcb == NULL);
    proc->tcb = threadcontroller_new(proc->sys);

    message("starting process '%s'", _process_getName(proc));

    /* now we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    /* exec the process and call main to start it */
    threadcontroller_start(proc->tcb, proc->argc, proc->argv);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    message("process '%s' started in %f seconds", _process_getName(proc), elapsed);

    _process_check(proc);
}

void process_continue(Process* proc) {
    MAGIC_ASSERT(proc);

    /* if we are not running, no need to notify anyone */
    if(!process_isRunning(proc)) {
        return;
    }

    info("switching to thread controller to continue executing process '%s'", _process_getName(proc));

    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    threadcontroller_continue(proc->tcb);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    info("process '%s' ran for %f seconds", _process_getName(proc), elapsed);

    _process_check(proc);
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    /* we only have state if we are running */
    if(!process_isRunning(proc)) {
        return;
    }

    message("terminating process '%s'", _process_getName(proc));

    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    threadcontroller_stop(proc->tcb);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    message("process '%s' stopped in %f seconds", _process_getName(proc), elapsed);

    _process_check(proc);
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

gboolean process_isRunning(Process* proc) {
    MAGIC_ASSERT(proc);
    return (proc->tcb != NULL && threadcontroller_isAlive(proc->tcb)) ? TRUE : FALSE;
}

gboolean process_wantsNotify(Process* proc, gint epollfd) {
    MAGIC_ASSERT(proc);
    // FIXME TODO XXX
    // how do we hook up notifations for epollfds?
    return FALSE;
    // old code:
//    if(process_isRunning(proc) && epollfd == proc->epollfd) {
//        return TRUE;
//    } else {
//        return FALSE;
//    }
}

static gint _process_getArguments(Process* proc, gchar** argvOut[]) {
    gchar* threadBuffer;

    GQueue *arguments = g_queue_new();

    /* first argument is the name of the program */
    const gchar* pluginName = proc->plugin.exeName ? proc->plugin.exeName->str : "NULL";
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

Process* process_new(SystemCallHandler* sys, guint processID,
        SimulationTime startTime, SimulationTime stopTime, const gchar* hostName,
        const gchar* pluginName, const gchar* pluginPath, const gchar* pluginSymbol,
        const gchar* preloadName, const gchar* preloadPath, gchar* arguments) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    utility_assert(sys);
    proc->sys = sys;
    syscallhandler_ref(proc->sys);

    proc->processID = processID;

    utility_assert(pluginName);
    proc->plugin.exeName = g_string_new(pluginName);
    utility_assert(pluginPath);
    proc->plugin.exePath = g_string_new(pluginPath);

    if(preloadName && preloadPath) {
        proc->plugin.preloadName = g_string_new(preloadName);
        proc->plugin.preloadPath = g_string_new(preloadPath);
    }

    proc->processName = g_string_new(NULL);
    g_string_printf(proc->processName, "%s.%s.%u",
            hostName,
            proc->plugin.exeName ? proc->plugin.exeName->str : "NULL",
            proc->processID);

    proc->startTime = startTime;
    proc->stopTime = stopTime;

    if(arguments && (g_ascii_strncasecmp(arguments, "\0", (gsize) 1) != 0)) {
        proc->arguments = g_string_new(arguments);
    }

    proc->argc = _process_getArguments(proc, &proc->argv);

    proc->cpuDelayTimer = g_timer_new();
    proc->referenceCount = 1;

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_NEW);

    return proc;
}

static void _process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    /* stop and free plugin memory if we are still running */
    if(proc->tcb) {
        if(threadcontroller_isAlive(proc->tcb)) {
            threadcontroller_stop(proc->tcb);
        }
        threadcontroller_unref(proc->tcb);
        proc->tcb = NULL;
    }

    if(proc->arguments) {
        g_string_free(proc->arguments, TRUE);
    }

    if(proc->plugin.exePath) {
        g_string_free(proc->plugin.exePath, TRUE);
    }
    if(proc->plugin.exeName) {
        g_string_free(proc->plugin.exeName, TRUE);
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

    if(proc->argv) {
        /* free the arguments */
        for(gint i = 0; i < proc->argc; i++) {
            g_free(proc->argv[i]);
        }
        g_free(proc->argv);
        proc->argv = NULL;
    }

    g_timer_destroy(proc->cpuDelayTimer);

    if(proc->sys) {
        syscallhandler_unref(proc->sys);
    }

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_FREE);

    MAGIC_CLEAR(proc);
    g_free(proc);
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
