/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */
#include "main/host/process.h"

#include <errno.h>
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
#include <stdbool.h>
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
#include "main/host/descriptor/descriptor_table.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timer.h"
#include "main/host/host.h"
#include "main/host/process.h"
#include "main/host/thread.h"
#include "main/host/thread_preload.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

#include "main/host/thread_ptrace.h"

struct _Process {
    /* Host owning this process */
    Host* host;

    /* unique id of the program that this process should run */
    guint processID;
    GString* processName;

    /* Which InterposeMethod to use for this process's threads */
    InterposeMethod interposeMethod;

    /* All of the descriptors opened by this process. */
    DescriptorTable* descTable;

    /* the shadow plugin executable */
    struct {
        /* the name and path to the executable that we will exec */
        GString* exeName;
        GString* exePath;

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

    /* vector of argument strings passed to exec */
    gchar** argv;
    /* vector of environment variables passed to exec */
    gchar** envv;

    gint returnCode;
    gboolean didLogReturnCode;

    /* the main execution unit for the plugin */
    Thread* mainThread;
    gint threadIDCounter;

    // TODO add spawned threads

    /* File descriptors to handle plugin out and err streams. */
    File* stdoutFile;
    File* stderrFile;

    gint referenceCount;
    MAGIC_DECLARE;
};

const gchar* process_getName(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(proc->processName->str);
    return proc->processName->str;
}

const gchar* process_getPluginName(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_assert(proc->plugin.exeName->str);
    return proc->plugin.exeName->str;
}

guint process_getProcessID(Process* proc) {
    MAGIC_ASSERT(proc);
    return proc->processID;
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
                        ((code == 0) ? "success" : "error"), code,
                        process_getName(proc));

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

    if(!proc->mainThread) {
        return;
    }

    if(thread_isRunning(proc->mainThread)) {
        info("process '%s' is running, but threads are blocked waiting for "
             "events",
             process_getName(proc));
    } else {
        /* collect return code */
        int returnCode = thread_getReturnCode(proc->mainThread);

        message("process '%s' has completed or is otherwise no longer running",
                process_getName(proc));
        _process_logReturnCode(proc, returnCode);

        thread_terminate(proc->mainThread);
        thread_unref(proc->mainThread);
        proc->mainThread = NULL;

        message("total runtime for process '%s' was %f seconds",
                process_getName(proc), proc->totalRunTime);
    }
}

static gchar* _process_outputFileName(Process* proc, const char* type) {
    return g_strdup_printf(
        "%s/%s.%s", host_getDataPath(proc->host), proc->processName->str, type);
}

static void _process_openStdIOFileHelper(Process* proc, bool isStdOut) {
    MAGIC_ASSERT(proc);

    gchar* fileName =
        _process_outputFileName(proc, isStdOut ? "stdout" : "stderr");

    File* stdfile = file_new();
    int errcode = file_open(stdfile, fileName, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (errcode < 0) {
        error("Opening %s: %s", fileName, strerror(-errcode));
        /* Unref and free the file object. */
        descriptor_close((Descriptor*)stdfile);
    } else {
        debug("Successfully opened %s file at %s",
              isStdOut ? "stdout" : "stderr", fileName);

        if (isStdOut) {
            descriptortable_set(
                proc->descTable, STDOUT_FILENO, (Descriptor*)stdfile);
            proc->stdoutFile = stdfile;
        } else {
            descriptortable_set(
                proc->descTable, STDERR_FILENO, (Descriptor*)stdfile);
            proc->stderrFile = stdfile;
        }

        /* Ref once since both the proc class and the table are storing it. */
        descriptor_ref((Descriptor*)stdfile);
    }

    g_free(fileName);
}

static void _process_start(Process* proc) {
    MAGIC_ASSERT(proc);

    /* dont do anything if we are already running */
    if(process_isRunning(proc)) {
        return;
    }

    // Set up stdout
    _process_openStdIOFileHelper(proc, true);

    // Set up stderr
    _process_openStdIOFileHelper(proc, false);

    utility_assert(proc->mainThread == NULL);
    if (proc->interposeMethod == INTERPOSE_PTRACE) {
        proc->mainThread =
            threadptrace_new(proc->host, proc, proc->threadIDCounter++);
    } else if (proc->interposeMethod == INTERPOSE_PRELOAD) {
        proc->mainThread =
            threadpreload_new(proc->host, proc, proc->threadIDCounter++);
    } else {
        error("Bad interposeMethod %d", proc->interposeMethod);
    }

    message("starting process '%s'", process_getName(proc));

    /* now we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    /* exec the process */
    thread_run(proc->mainThread, proc->argv, proc->envv);
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    message(
        "process '%s' started in %f seconds", process_getName(proc), elapsed);

    /* call main and run until blocked */
    process_continue(proc, proc->mainThread);

    _process_check(proc);
}

void process_continue(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);

    /* if we are not running, no need to notify anyone */
    if(!process_isRunning(proc)) {
        return;
    }

    info("switching to thread controller to continue executing process '%s'",
         process_getName(proc));

    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    SysCallCondition* cond = thread_resume(thread ? thread : proc->mainThread);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    info("process '%s' ran for %f seconds", process_getName(proc), elapsed);

    /* If the thread has an unblocking condition, listen for it. */
    if (cond) {
        syscallcondition_waitNonblock(
            cond, proc, thread ? thread : proc->mainThread);
    }

    _process_check(proc);
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    message("terminating process '%s'", process_getName(proc));

    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    if (proc->mainThread) {
        thread_terminate(proc->mainThread);
        thread_unref(proc->mainThread);
        proc->mainThread = NULL;
    }
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    descriptortable_shutdownHelper(proc->descTable);

    worker_setActiveProcess(NULL);

    message(
        "process '%s' stopped in %f seconds", process_getName(proc), elapsed);

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
    return (proc->mainThread != NULL && thread_isRunning(proc->mainThread)) ? TRUE : FALSE;
}

Process* process_new(Host* host, guint processID, SimulationTime startTime,
                     SimulationTime stopTime, InterposeMethod interposeMethod,
                     const gchar* hostName, const gchar* pluginName,
                     const gchar* pluginPath, const gchar* pluginSymbol,
                     gchar** envv, gchar** argv) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    proc->host = host;
    host_ref(proc->host);

    proc->processID = processID;

    /* plugin name and path are required so we know what to execute */
    utility_assert(pluginName);
    utility_assert(pluginPath);
    proc->plugin.exeName = g_string_new(pluginName);
    proc->plugin.exePath = g_string_new(pluginPath);

    proc->processName = g_string_new(NULL);
    g_string_printf(proc->processName, "%s.%s.%u",
            hostName,
            proc->plugin.exeName ? proc->plugin.exeName->str : "NULL",
            proc->processID);

    proc->cpuDelayTimer = g_timer_new();

    proc->startTime = startTime;
    proc->stopTime = stopTime;

    proc->interposeMethod = interposeMethod;

    /* add log file to env */
    {
        gchar* logFileName = _process_outputFileName(proc, "shimlog");
        envv = g_environ_setenv(envv, "SHADOW_LOG_FILE", logFileName, TRUE);
        g_free(logFileName);
    }

    /* save args and env */
    proc->argv = argv;
    proc->envv = envv;

    proc->descTable = descriptortable_new();

    proc->referenceCount = 1;

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_NEW);

    return proc;
}

static void _process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    /* stop and free plugin memory if we are still running */
    if(proc->mainThread) {
        if(thread_isRunning(proc->mainThread)) {
            thread_terminate(proc->mainThread);
        }
        thread_unref(proc->mainThread);
        proc->mainThread = NULL;
    }

    if(proc->plugin.exePath) {
        g_string_free(proc->plugin.exePath, TRUE);
    }
    if(proc->plugin.exeName) {
        g_string_free(proc->plugin.exeName, TRUE);
    }
    if(proc->processName) {
        g_string_free(proc->processName, TRUE);
    }

    if(proc->argv) {
        g_strfreev(proc->argv);
    }
    if(proc->envv) {
        g_strfreev(proc->envv);
    }

    g_timer_destroy(proc->cpuDelayTimer);

    /* Free the stdio files before the descriptor table.
     * Closing the descriptors will remove them from the table and the table
     * will release it's ref. We also need to release our proc ref. */
    if (proc->stderrFile) {
        descriptor_close((Descriptor*)proc->stderrFile);
        descriptor_unref((Descriptor*)proc->stderrFile);
    }
    if (proc->stdoutFile) {
        descriptor_close((Descriptor*)proc->stdoutFile);
        descriptor_unref((Descriptor*)proc->stdoutFile);
    }

    /* Now free all remaining descriptors stored in our table. */
    if (proc->descTable) {
        descriptortable_unref(proc->descTable);
    }

    /* And we no longer need to access the host. */
    if (proc->host) {
        host_unref(proc->host);
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

// ******************************************************
// Handler the descriptors owned by this process
// ******************************************************

int process_registerDescriptor(Process* proc, Descriptor* desc) {
    MAGIC_ASSERT(proc);
    utility_assert(desc);
    descriptor_setOwnerProcess(desc, proc);
    return descriptortable_add(proc->descTable, desc);
}

void process_deregisterDescriptor(Process* proc, Descriptor* desc) {
    MAGIC_ASSERT(proc);

    if (desc) {
        DescriptorType dType = descriptor_getType(desc);
        if (dType == DT_TCPSOCKET || dType == DT_UDPSOCKET) {
            host_disassociateInterface(proc->host, (Socket*)desc);
        }
        descriptor_setOwnerProcess(desc, NULL);
        descriptortable_remove(proc->descTable, desc);
    }
}

Descriptor* process_getRegisteredDescriptor(Process* proc, int handle) {
    MAGIC_ASSERT(proc);
    return descriptortable_get(proc->descTable, handle);
}

