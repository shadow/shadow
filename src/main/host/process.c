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
#include "main/bindings/c/bindings.h"
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
#include "main/host/syscall_condition.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/host/thread_preload.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/random.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

#include "main/host/thread_ptrace.h"

static gchar* _process_outputFileName(Process* proc, const char* type);

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

    // int thread_id -> Thread*.
    GHashTable* threads;

    // Owned exclusively by the process.
    MemoryManager* memoryManager;

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

static void _process_reapThread(Process* process, Thread* thread) {
    thread_terminate(thread);

    // If the `clear_child_tid` attribute on the thread is set, perform a futex
    // wake on that address. This mechanism is typically used in `pthread_join`
    // etc.  See `set_tid_address(2)`.
    PluginVirtualPtr clear_child_tid_pvp = thread_getTidAddress(thread);
    if (clear_child_tid_pvp.val) {
        pid_t* clear_child_tid =
            process_getWriteablePtr(process, thread, clear_child_tid_pvp, sizeof(pid_t*));
        if (!clear_child_tid) {
            // We *might* end up getting here (or failing even earlier) if we end up having to use
            // thread_getWriteablePtr (i.e. because the address isn't shared in the
            // MemoryManager), since the native thread (and maybe the whole
            // process) is no longer alive. If so, the most straightforward
            // fix might be to extend the MemoryManager to include the region
            // containing the tid pointer in this case.
            //
            // Alternatively we could try to use a still-living thread (if any)
            // to do the memory write, and just skip if there are no more
            // living threads in the process. Probably better to avoid that
            // complexity if we can, though.
            error("Couldn't clear child tid; See code comments.");
            abort();
        }
        *clear_child_tid = 0;

        FutexTable* ftable = host_getFutexTable(process->host);
        utility_assert(ftable);
        Futex* futex =
            futextable_get(ftable, process_getPhysicalAddress(process, clear_child_tid_pvp));
        if (futex) {
            futex_wake(futex, 1);
        }
    }

    if (thread_isLeader(thread)) {
        // If the main thread has exited, grab its return code to be used as
        // the process return code.  The main thread exiting doesn't
        // automatically cause other threads and the process to exit, but in
        // most cases they will exit shortly afterwards.
        //
        // We don't *log* the returnCode yet because other conditions could
        // still change it.  e.g. experimentally in the bash shell, if the
        // remaining children are killed by a signal, the return code for the
        // whole process is derived from that signal #, even if the thread
        // leader had already exited.
        process->returnCode = thread_getReturnCode(thread);
    }
}

static void _process_terminate_threads(Process* proc) {
    GHashTableIter iter;
    g_hash_table_iter_init(&iter, proc->threads);
    gpointer key, value;
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        Thread* thread = value;
        if (thread_isRunning(thread)) {
            warning("Terminating still-running thread %d", thread_getID(thread));
        }
        _process_reapThread(proc, thread);
        g_hash_table_iter_remove(&iter);
    }
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
        // don't change the formatting of this string since some integration tests depend on it
        GString* mainResultString = g_string_new(NULL);
        g_string_printf(mainResultString, "main %s code '%i' for process '%s'",
                        ((code == 0) ? "success" : "error"), code,
                        process_getName(proc));

        gchar* fileName = _process_outputFileName(proc, "exitcode");
        FILE *exitcodeFile = fopen(fileName, "we");
        g_free(fileName);

        if (exitcodeFile != NULL) {
            fprintf(exitcodeFile, "%d", code);
            fclose(exitcodeFile);
        } else {
            warning("Could not open '%s' for writing: %s", mainResultString->str, strerror(errno));
        }

        // if there was no error or was intentionally killed
        // TODO: once we've implemented clean shutdown via SIGTERM,
        //       treat death by SIGKILL as a plugin error
        if (code == 0 || code == return_code_for_signal(SIGKILL)) {
            message("%s", mainResultString->str);
        } else {
            warning("%s", mainResultString->str);
            worker_incrementPluginError();
        }

        g_string_free(mainResultString, TRUE);

        proc->didLogReturnCode = TRUE;
    }
}

static Thread* _process_threadLeader(Process* proc) {
    // "main" thread is the one where pid==tid.
    return g_hash_table_lookup(proc->threads, GUINT_TO_POINTER(proc->processID));
}

static void _process_check(Process* proc) {
    MAGIC_ASSERT(proc);

    if (process_isRunning(proc)) {
        return;
    }

    message("process '%s' has completed or is otherwise no longer running", process_getName(proc));
    _process_logReturnCode(proc, proc->returnCode);
    message(
        "total runtime for process '%s' was %f seconds", process_getName(proc), proc->totalRunTime);
}

static void _process_check_thread(Process* proc, Thread* thread) {
    if (thread_isRunning(thread)) {
        info("thread %d in process '%s' still running, but blocked", thread_getID(thread),
             process_getName(proc));
        return;
    }
    int returnCode = thread_getReturnCode(thread);
    info("thread %d in process '%s' exited with code %d", thread_getID(thread),
         process_getName(proc), returnCode);
    _process_reapThread(proc, thread);
    g_hash_table_remove(proc->threads, GUINT_TO_POINTER(thread_getID(thread)));
    _process_check(proc);
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

    // tid of first thread of a process is equal to the pid.
    int tid = proc->processID;
    Thread* mainThread = NULL;
    if (proc->interposeMethod == INTERPOSE_PRELOAD_PTRACE) {
        mainThread = threadptrace_new(proc->host, proc, tid);
    } else if (proc->interposeMethod == INTERPOSE_PTRACE_ONLY) {
        mainThread = threadptraceonly_new(proc->host, proc, tid);
    } else if (proc->interposeMethod == INTERPOSE_PRELOAD_ONLY) {
        mainThread = threadpreload_new(proc->host, proc, tid);
    } else {
        error("Bad interposeMethod %d", proc->interposeMethod);
    }
    g_hash_table_insert(proc->threads, GUINT_TO_POINTER(tid), mainThread);

    message("starting process '%s'", process_getName(proc));

    /* now we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    /* exec the process */
    thread_run(mainThread, proc->argv, proc->envv);
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    message(
        "process '%s' started in %f seconds", process_getName(proc), elapsed);

    /* call main and run until blocked */
    process_continue(proc, mainThread);
}

static void _start_thread_task(gpointer callbackObject, gpointer callbackArgument) {
    Process* process = callbackObject;
    Thread* thread = callbackArgument;
    process_continue(process, thread);
}

static void _start_thread_task_free_process(gpointer data) {
    Process* process = data;
    process_unref(process);
}

static void _start_thread_task_free_thread(gpointer data) {
    Thread* thread = data;
    thread_unref(thread);
}

void process_addThread(Process* proc, Thread* thread) {
    g_hash_table_insert(proc->threads, GUINT_TO_POINTER(thread_getID(thread)), thread);

    // Schedule thread to start.
    thread_ref(thread);
    process_ref(proc);
    Task* task = task_new(_start_thread_task, proc, thread, _start_thread_task_free_process,
                          _start_thread_task_free_thread);
    worker_scheduleTask(task, 0);
    task_unref(task);
}

void process_continue(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);
    debug("Continuing thread %d in process %d", thread_getID(thread), proc->processID);

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
    thread_resume(thread);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    worker_setActiveProcess(NULL);

    info("process '%s' ran for %f seconds", process_getName(proc), elapsed);

    _process_check_thread(proc, thread);
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    message("terminating process '%s'", process_getName(proc));

    worker_setActiveProcess(proc);

    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);

    proc->plugin.isExecuting = TRUE;
    _process_terminate_threads(proc);
    proc->plugin.isExecuting = FALSE;

    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);

    debug("Starting descriptor table shutdown hack");
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

void process_detachPlugin(gpointer procptr, gpointer nothing) {
    Process* proc = procptr;
    MAGIC_ASSERT(proc);
    if (proc->interposeMethod == INTERPOSE_PRELOAD_PTRACE ||
        proc->interposeMethod == INTERPOSE_PTRACE_ONLY) {
        GHashTableIter iter;
        g_hash_table_iter_init(&iter, proc->threads);
        gpointer key, value;
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            Thread* thread = value;
            worker_setActiveProcess(proc);
            threadptrace_detach(thread);
            worker_setActiveProcess(NULL);
        }
    }
}

gboolean process_isRunning(Process* proc) {
    MAGIC_ASSERT(proc);
    return g_hash_table_size(proc->threads) > 0;
}

static void _thread_gpointer_unref(gpointer data) { thread_unref(data); }

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

    proc->threads =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, _thread_gpointer_unref);

    proc->referenceCount = 1;

    worker_countObject(OBJECT_TYPE_PROCESS, COUNTER_TYPE_NEW);

    return proc;
}

static void _process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    _process_terminate_threads(proc);
    if (proc->threads) {
        g_hash_table_destroy(proc->threads);
        proc->threads = NULL;
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
    if (proc->memoryManager) {
        memorymanager_free(proc->memoryManager);
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

MemoryManager* process_getMemoryManager(Process* proc) {
    MAGIC_ASSERT(proc);
    return proc->memoryManager;
}

void process_setMemoryManager(Process* proc, MemoryManager* memoryManager) {
    MAGIC_ASSERT(proc);
    if (proc->memoryManager) {
        memorymanager_free(proc->memoryManager);
    }
    proc->memoryManager = memoryManager;
}

PluginPhysicalPtr process_getPhysicalAddress(Process* proc, PluginVirtualPtr vPtr) {
    // TODO: actually do a conversion that gets us as close as we can to a unique pointer
    return (PluginPhysicalPtr){.val = vPtr.val};
}

const void* process_getReadablePtr(Process* proc, Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    if (proc->memoryManager) {
        return memorymanager_getReadablePtr(proc->memoryManager, thread, plugin_src, n);
    } else {
        return thread_getReadablePtr(thread, plugin_src, n);
    }
}

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getWriteablePtr(Process* proc, Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    if (proc->memoryManager) {
        return memorymanager_getWriteablePtr(proc->memoryManager, thread, plugin_src, n);
    } else {
        return thread_getWriteablePtr(thread, plugin_src, n);
    }
}

// Returns a writeable pointer corresponding to the specified src. Use when
// the data at the given address needs to be both read and written.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getMutablePtr(Process* proc, Thread* thread, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    if (proc->memoryManager) {
        return memorymanager_getMutablePtr(proc->memoryManager, thread, plugin_src, n);
    } else {
        return thread_getMutablePtr(thread, plugin_src, n);
    }
}

// Flushes and invalidates all previously returned readable/writeable plugin
// pointers, as if returning control to the plugin. This can be useful in
// conjunction with `thread_nativeSyscall` operations that touch memory.
void process_flushPtrs(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);
    thread_flushPtrs(thread);
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

