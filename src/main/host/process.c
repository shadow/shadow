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
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include "glib/gprintf.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/definitions.h"
#include "main/core/work/task.h"
#include "main/core/worker.h"
#include "main/host/cpu.h"
#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/compat_socket.h"
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

// We normally attempt to serve hot-path syscalls on the shim-side to avoid a
// more expensive inter-process syscall. This option disables the optimization.
// This is defined here in Shadow because it breaks the shim.
static bool _disable_shim_syscall_handler = false;
OPTION_EXPERIMENTAL_ENTRY("disable-shim-syscall-handler", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE,
                          &_disable_shim_syscall_handler,
                          "Disable shim-side syscall handler to force hot-path syscalls to be "
                          "handled via an inter-process syscall with shadow.",
                          NULL)

static gchar* _process_outputFileName(Process* proc, const char* type);
static void _process_check(Process* proc);

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

#ifdef USE_PERF_TIMERS
    /* timer that tracks the amount of CPU time we spend on plugin execution and processing */
    GTimer* cpuDelayTimer;
    gdouble totalRunTime;
#endif

    /* process boot and shutdown variables */
    SimulationTime startTime;
    SimulationTime stopTime;

    /* absolute path to the process's working directory */
    char* workingDir;

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

    /* When true, threads are no longer runnable and should just be cleaned up. */
    bool isExiting;

    /* Native pid of the process */
    pid_t nativePid;

    gint referenceCount;
    MAGIC_DECLARE;
};

static Thread* _process_threadLeader(Process* proc) {
    // "main" thread is the one where pid==tid.
    return g_hash_table_lookup(proc->threads, GUINT_TO_POINTER(proc->processID));
}

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

const char* process_getWorkingDir(Process* proc) {
    MAGIC_ASSERT(proc);
    return proc->workingDir;
}

guint process_getProcessID(Process* proc) {
    MAGIC_ASSERT(proc);
    return proc->processID;
}

static void _process_reapThread(Process* process, Thread* thread) {
    utility_assert(!thread_isRunning(thread));

    // If the `clear_child_tid` attribute on the thread is set, and there are
    // any other threads left alive in the process, perform a futex wake on
    // that address. This mechanism is typically used in `pthread_join` etc.
    // See `set_tid_address(2)`.
    PluginVirtualPtr clear_child_tid_pvp = thread_getTidAddress(thread);
    if (clear_child_tid_pvp.val && g_hash_table_size(process->threads) > 1 && !process->isExiting) {
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
        process_flushPtrs(process, thread);

        FutexTable* ftable = host_getFutexTable(process->host);
        utility_assert(ftable);
        Futex* futex =
            futextable_get(ftable, process_getPhysicalAddress(process, clear_child_tid_pvp));
        if (futex) {
            futex_wake(futex, 1);
        }
    }
}

static void _process_handleProcessExit(Process* proc) {
    debug("handleProcessExit");
    utility_assert(!process_isRunning(proc));

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, proc->threads);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        Thread* thread = value;
        thread_handleProcessExit(thread);
        utility_assert(!thread_isRunning(thread));
        _process_reapThread(proc, thread);

        // Must be last, since it unrefs the thread.
        g_hash_table_iter_remove(&iter);
    }

    _process_check(proc);
}

static void _process_terminate_threads(Process* proc) {
    debug("Terminating threads");
    if (process_isRunning(proc)) {
        if (kill(proc->nativePid, SIGKILL)) {
            warning("kill(pid=%d) error %d: %s", proc->nativePid, errno, g_strerror(errno));
        }
        process_markAsExiting(proc);
    }

    _process_handleProcessExit(proc);
}

#ifdef USE_PERF_TIMERS
static void _process_handleTimerResult(Process* proc, gdouble elapsedTimeSec) {
    SimulationTime delay = (SimulationTime) (elapsedTimeSec * SIMTIME_ONE_SECOND);
    Host* currentHost = worker_getActiveHost();
    cpu_addDelay(host_getCPU(currentHost), delay);
    tracker_addProcessingTime(host_getTracker(currentHost), delay);
    proc->totalRunTime += elapsedTimeSec;
}
#endif

static void _process_getAndLogReturnCode(Process* proc) {
    if(!proc->didLogReturnCode) {
        // Return an error if we can't get real exit code.
        proc->returnCode = EXIT_FAILURE;

        int wstatus = 0;
        int rv = waitpid(proc->nativePid, &wstatus, __WALL);
        if (rv < 0) {
            // Getting here is a bug, but since the process is exiting anyway
            // not serious enough to merit `error`ing out.
            warning("waitpid: %s", g_strerror(errno));
        } else if (rv != proc->nativePid) {
            warning("waitpid returned %d instead of the requested %d", rv, proc->nativePid);
        } else {
            if (WIFEXITED(wstatus)) {
                proc->returnCode = WEXITSTATUS(wstatus);
            } else if (WIFSIGNALED(wstatus)) {
                proc->returnCode = return_code_for_signal(WTERMSIG(wstatus));
            } else {
                warning("Couldn't get exit status");
            }
        }

        // don't change the formatting of this string since some integration tests depend on it
        GString* mainResultString = g_string_new(NULL);
        g_string_printf(mainResultString, "main %s code '%i' for process '%s'",
                        ((proc->returnCode == 0) ? "success" : "error"), proc->returnCode,
                        process_getName(proc));

        gchar* fileName = _process_outputFileName(proc, "exitcode");
        FILE *exitcodeFile = fopen(fileName, "we");
        g_free(fileName);

        if (exitcodeFile != NULL) {
            fprintf(exitcodeFile, "%d", proc->returnCode);
            fclose(exitcodeFile);
        } else {
            warning("Could not open '%s' for writing: %s", mainResultString->str, strerror(errno));
        }

        // if there was no error or was intentionally killed
        // TODO: once we've implemented clean shutdown via SIGTERM,
        //       treat death by SIGKILL as a plugin error
        if (proc->returnCode == 0 || proc->returnCode == return_code_for_signal(SIGKILL)) {
            message("%s", mainResultString->str);
        } else {
            warning("%s", mainResultString->str);
            worker_incrementPluginError();
        }

        g_string_free(mainResultString, TRUE);

        proc->didLogReturnCode = TRUE;
    }
}

pid_t process_findNativeTID(Process* proc, pid_t virtualPID, pid_t virtualTID) {
    MAGIC_ASSERT(proc);

    Thread* thread = NULL;

    if (virtualPID > 0 && virtualTID > 0) {
        // Both PID and TID must match
        if (proc->processID == virtualPID) {
            thread = g_hash_table_lookup(proc->threads, GINT_TO_POINTER(virtualTID));
        }
    } else if (virtualPID > 0) {
        // Get the TID of the main thread if the PID matches
        if (proc->processID == virtualPID) {
            thread = _process_threadLeader(proc);
        }
    } else if (virtualTID > 0) {
        // Get the TID of any thread that matches, ignoring PID
        thread = g_hash_table_lookup(proc->threads, GINT_TO_POINTER(virtualTID));
    }

    if (thread != NULL) {
        return thread_getNativeTid(thread);
    } else {
        return 0; // not found
    }
}

static void _process_check(Process* proc) {
    MAGIC_ASSERT(proc);

    if (process_isRunning(proc)) {
        return;
    }

    message("process '%s' has completed or is otherwise no longer running", process_getName(proc));
    _process_getAndLogReturnCode(proc);
#ifdef USE_PERF_TIMERS
    message(
        "total runtime for process '%s' was %f seconds", process_getName(proc), proc->totalRunTime);
#endif
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

static File* _process_openStdIOFileHelper(Process* proc, int fd, gchar* fileName) {
    MAGIC_ASSERT(proc);
    utility_assert(fileName != NULL);

    File* stdfile = file_new();

    char* cwd = getcwd(NULL, 0);
    if (!cwd) {
        error("getcwd unable to allocate string buffer, error %i: %s", errno, strerror(errno));
    }

    int errcode = file_open(stdfile, fileName, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, cwd);
    free(cwd);

    if (errcode < 0) {
        error("Opening %s: %s", fileName, strerror(-errcode));
        /* Unref and free the file object. */
        descriptor_close((LegacyDescriptor*)stdfile);
    } else {
        debug("Successfully opened fd %d at %s", fd, fileName);

        CompatDescriptor* compatDesc = compatdescriptor_fromLegacy((LegacyDescriptor*)stdfile);
        descriptortable_set(proc->descTable, fd, compatDesc);
    }

    return stdfile;
}

static void _process_start(Process* proc) {
    MAGIC_ASSERT(proc);

    /* dont do anything if we are already running */
    if(process_isRunning(proc)) {
        return;
    }

    // Set up stdin
    _process_openStdIOFileHelper(proc, STDIN_FILENO, "/dev/null");

    // Set up stdout
    gchar* stdoutFileName = _process_outputFileName(proc, "stdout");
    proc->stdoutFile = _process_openStdIOFileHelper(proc, STDOUT_FILENO, stdoutFileName);
    descriptor_ref((LegacyDescriptor*)proc->stdoutFile);
    g_free(stdoutFileName);

    // Set up stderr
    gchar* stderrFileName = _process_outputFileName(proc, "stderr");
    proc->stderrFile = _process_openStdIOFileHelper(proc, STDERR_FILENO, stderrFileName);
    descriptor_ref((LegacyDescriptor*)proc->stderrFile);
    g_free(stderrFileName);

    // tid of first thread of a process is equal to the pid.
    int tid = proc->processID;
    Thread* mainThread = NULL;
    if (proc->interposeMethod == INTERPOSE_HYBRID) {
        mainThread = threadptrace_new(proc->host, proc, tid);
    } else if (proc->interposeMethod == INTERPOSE_PTRACE) {
        mainThread = threadptraceonly_new(proc->host, proc, tid);
    } else if (proc->interposeMethod == INTERPOSE_PRELOAD) {
        mainThread = threadpreload_new(proc->host, proc, tid);
    } else {
        error("Bad interposeMethod %d", proc->interposeMethod);
    }
    g_hash_table_insert(proc->threads, GUINT_TO_POINTER(tid), mainThread);

    message("starting process '%s'", process_getName(proc));

    /* now we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);

#ifdef USE_PERF_TIMERS
    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);
#endif

    proc->plugin.isExecuting = TRUE;
    /* exec the process */
    thread_run(mainThread, proc->argv, proc->envv, proc->workingDir);
    proc->nativePid = thread_getNativePid(mainThread);
#ifdef USE_PERF_TIMERS
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);
#endif

    worker_setActiveProcess(NULL);

#ifdef USE_PERF_TIMERS
    message(
        "process '%s' started in %f seconds", process_getName(proc), elapsed);
#else
    message("process '%s' started", process_getName(proc));
#endif

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
    MAGIC_ASSERT(proc);
    g_hash_table_insert(proc->threads, GUINT_TO_POINTER(thread_getID(thread)), thread);

    // Schedule thread to start.
    thread_ref(thread);
    process_ref(proc);
    Task* task = task_new(_start_thread_task, proc, thread, _start_thread_task_free_process,
                          _start_thread_task_free_thread);
    worker_scheduleTask(task, 0);
    task_unref(task);
}

void process_markAsExiting(Process* proc) {
    MAGIC_ASSERT(proc);
    debug("Process %d marked as exiting", proc->processID);
    proc->isExiting = true;
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

#ifdef USE_PERF_TIMERS
    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);
#endif

    proc->plugin.isExecuting = TRUE;
    thread_resume(thread);
    proc->plugin.isExecuting = FALSE;

#ifdef USE_PERF_TIMERS
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);
#endif

    worker_setActiveProcess(NULL);

#ifdef USE_PERF_TIMERS
    info("process '%s' ran for %f seconds", process_getName(proc), elapsed);
#else
    info("process '%s' done continuing", process_getName(proc));
#endif

    if (proc->isExiting) {
        // If the whole process is already exiting, skip to cleaning up the
        // whole process exit; normal thread cleanup would likely fail.
        _process_handleProcessExit(proc);
    } else {
        _process_check_thread(proc, thread);
    }
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    message("terminating process '%s'", process_getName(proc));

    worker_setActiveProcess(proc);

#ifdef USE_PERF_TIMERS
    /* time how long we execute the program */
    g_timer_start(proc->cpuDelayTimer);
#endif

    proc->plugin.isExecuting = TRUE;
    _process_terminate_threads(proc);
    proc->plugin.isExecuting = FALSE;

#ifdef USE_PERF_TIMERS
    gdouble elapsed = g_timer_elapsed(proc->cpuDelayTimer, NULL);
    _process_handleTimerResult(proc, elapsed);
#endif

    debug("Starting descriptor table shutdown hack");
    descriptortable_shutdownHelper(proc->descTable);

    worker_setActiveProcess(NULL);

#ifdef USE_PERF_TIMERS
    message(
        "process '%s' stopped in %f seconds", process_getName(proc), elapsed);
#else
    message("process '%s' stopped", process_getName(proc));
#endif

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
    if (proc->interposeMethod == INTERPOSE_HYBRID ||
        proc->interposeMethod == INTERPOSE_PTRACE) {
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
    return !proc->isExiting && g_hash_table_size(proc->threads) > 0;
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

#ifdef USE_PERF_TIMERS
    proc->cpuDelayTimer = g_timer_new();
#endif

    proc->startTime = startTime;
    proc->stopTime = stopTime;

    proc->interposeMethod = interposeMethod;

    proc->workingDir = realpath(host_getDataPath(host), NULL);

    if (proc->workingDir == NULL) {
        error("Could not allocate memory for the process' working directory, or directory did not "
              "exist");
    }

    /* add log file to env */
    {
        gchar* logFileName = _process_outputFileName(proc, "shimlog");
        envv = g_environ_setenv(envv, "SHADOW_LOG_FILE", logFileName, TRUE);
        g_free(logFileName);
    }

    if (_disable_shim_syscall_handler) {
        envv = g_environ_setenv(envv, "SHADOW_DISABLE_SHIM_SYSCALL", "TRUE", TRUE);
    }

    /* save args and env */
    proc->argv = argv;
    proc->envv = envv;

    proc->descTable = descriptortable_new();

    proc->threads =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, _thread_gpointer_unref);

    proc->referenceCount = 1;
    proc->isExiting = false;

    worker_count_allocation(Process);

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

    if (proc->workingDir) {
        free(proc->workingDir);
    }

    if(proc->argv) {
        g_strfreev(proc->argv);
    }
    if(proc->envv) {
        g_strfreev(proc->envv);
    }

#ifdef USE_PERF_TIMERS
    g_timer_destroy(proc->cpuDelayTimer);
#endif

    /* Free the stdio files before the descriptor table.
     * Closing the descriptors will remove them from the table and the table
     * will release it's ref. We also need to release our proc ref. */
    if (proc->stderrFile) {
        descriptor_close((LegacyDescriptor*)proc->stderrFile);
        descriptor_unref((LegacyDescriptor*)proc->stderrFile);
    }
    if (proc->stdoutFile) {
        descriptor_close((LegacyDescriptor*)proc->stdoutFile);
        descriptor_unref((LegacyDescriptor*)proc->stdoutFile);
    }

    /* Now free all remaining descriptors stored in our table. */
    if (proc->descTable) {
        descriptortable_unref(proc->descTable);
    }

    /* And we no longer need to access the host. */
    if (proc->host) {
        host_unref(proc->host);
    }

    worker_count_deallocation(Process);

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
    // We currently don't keep a true system-wide virtual <-> physical address
    // mapping. Instead we simply assume that no shadow processes map the same
    // underlying physical memory, and that therefore (pid, virtual address)
    // uniquely defines a physical address.
    //
    // If we ever want to support futexes in memory shared between processes,
    // we'll need to change this.  The most foolproof way to do so is probably
    // to change PluginPhysicalPtr to be a bigger struct that identifies where
    // the mapped region came from (e.g. what file), and the offset into that
    // region. Such "fat" physical pointers might make memory management a
    // little more cumbersome though, e.g. when using them as keys in the futex
    // table.
    //
    // Alternatively we could hash the region+offset to a 64-bit value, but
    // then we'd need to deal with potential collisions. On average we'd expect
    // a collision after 2**32 physical addresses; i.e. they *probably*
    // wouldn't happen in practice for realistic simulations.

    // Linux uses the bottom 48-bits for user-space virtual addresses, giving
    // us 16 bits for the pid.
    const int pid_bits = 16;

    guint pid = process_getProcessID(proc);
    const int pid_shift = 64 - pid_bits;
    uint64_t high = (uint64_t)pid << pid_shift;
    utility_assert(high >> pid_shift == pid);

    uint64_t low = vPtr.val;
    utility_assert(low >> pid_shift == 0);

    return (PluginPhysicalPtr){.val = low | high};
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
// Handle the descriptors owned by this process
// ******************************************************

int process_registerCompatDescriptor(Process* proc, CompatDescriptor* compatDesc) {
    MAGIC_ASSERT(proc);
    utility_assert(compatDesc);
    return descriptortable_add(proc->descTable, compatDesc);
}

CompatDescriptor* process_deregisterCompatDescriptor(Process* proc, int handle) {
    MAGIC_ASSERT(proc);
    return descriptortable_remove(proc->descTable, handle);
}

CompatDescriptor* process_getRegisteredCompatDescriptor(Process* proc, int handle) {
    MAGIC_ASSERT(proc);
    CompatDescriptor* compatDesc = descriptortable_get(proc->descTable, handle);
    return compatDesc;
}

int process_registerLegacyDescriptor(Process* proc, LegacyDescriptor* desc) {
    MAGIC_ASSERT(proc);
    utility_assert(desc);

    descriptor_setOwnerProcess(desc, proc);
    CompatDescriptor* compatDesc = compatdescriptor_fromLegacy(desc);

    return process_registerCompatDescriptor(proc, compatDesc);
}

void process_deregisterLegacyDescriptor(Process* proc, LegacyDescriptor* desc) {
    MAGIC_ASSERT(proc);

    if (desc) {
        LegacyDescriptorType dType = descriptor_getType(desc);
        if (dType == DT_TCPSOCKET || dType == DT_UDPSOCKET) {
            CompatSocket compat_socket = compatsocket_fromLegacySocket((Socket*)desc);
            host_disassociateInterface(proc->host, &compat_socket);
        }
        descriptor_setOwnerProcess(desc, NULL);
        CompatDescriptor* compatDesc =
            descriptortable_remove(proc->descTable, descriptor_getHandle(desc));
        compatdescriptor_free(compatDesc);
    }
}

LegacyDescriptor* process_getRegisteredLegacyDescriptor(Process* proc, int handle) {
    MAGIC_ASSERT(proc);

    CompatDescriptor* compatDesc = process_getRegisteredCompatDescriptor(proc, handle);
    if (compatDesc == NULL) {
        return NULL;
    }

    // will return NULL if the descriptor is valid but is not a legacy descriptor
    LegacyDescriptor* legacyDesc = compatdescriptor_asLegacy(compatDesc);

    if (legacyDesc == NULL) {
        warning("A descriptor exists for fd=%d, but it is not a legacy descriptor. Returning NULL.",
                handle);
    }

    return legacyDesc;
}
