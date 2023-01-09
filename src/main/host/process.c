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
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syscall.h>
#include <time.h>
#include <unistd.h>

#include "glib/gprintf.h"
#include "lib/logger/logger.h"
#include "main/bindings/c/bindings.h"
#include "main/core/support/config_handlers.h"
#include "main/core/support/definitions.h"
#include "main/core/worker.h"
#include "main/host/descriptor/compat_socket.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/host/descriptor/regular_file.h"
#include "main/host/descriptor/socket.h"
#include "main/host/descriptor/tcp.h"
#include "main/host/descriptor/timerfd.h"
#include "main/host/managed_thread.h"
#include "main/host/process.h"
#include "main/host/shimipc.h"
#include "main/host/syscall_condition.h"
#include "main/host/syscall_types.h"
#include "main/host/thread.h"
#include "main/host/tracker.h"
#include "main/routing/address.h"
#include "main/routing/dns.h"
#include "main/utility/utility.h"

struct _Process {
    /* Pointer to the RustProcess that owns this Process */
    const RustProcess* rustProcess;

    MAGIC_DECLARE;
};

static void _unref_thread_cb(gpointer data);

static const Host* _host(Process* proc) {
    const Host* host = worker_getCurrentHost();
    utility_debugAssert(host_getID(host) == process_getHostId(proc));
    return host;
}

const ShimShmemProcess* process_getSharedMem(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getSharedMem(proc->rustProcess);
}

const gchar* process_getName(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getName(proc->rustProcess);
}

StraceFmtMode process_straceLoggingMode(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_straceLoggingMode(proc->rustProcess);
}

int process_getStraceFd(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_straceFd(proc->rustProcess);
}

const gchar* process_getPluginName(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getPluginName(proc->rustProcess);
}

const char* process_getWorkingDir(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getWorkingDir(proc->rustProcess);
}

pid_t process_getProcessID(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getProcessID(proc->rustProcess);
}

pid_t process_getNativePid(const Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getNativePid(proc->rustProcess);
}

void process_reapThread(Process* process, Thread* thread) {
    utility_debugAssert(!thread_isRunning(thread));

    // If the `clear_child_tid` attribute on the thread is set, and there are
    // any other threads left alive in the process, perform a futex wake on
    // that address. This mechanism is typically used in `pthread_join` etc.
    // See `set_tid_address(2)`.
    PluginVirtualPtr clear_child_tid_pvp = thread_getTidAddress(thread);
    if (clear_child_tid_pvp.val && _process_numThreads(process->rustProcess) > 1 && !_process_isExiting(process->rustProcess)) {
        // Verify thread is really dead. This *should* no longer be needed, but doesn't
        // hurt to defensively do anyway, since waking the futex before the thread has
        // actually exited can (and has) led to difficult-to-track-down bugs.
        while (1) {
            pid_t pid = thread_getNativePid(thread);
            pid_t tid = thread_getNativeTid(thread);
            int rv = (int)syscall(SYS_tgkill, pid, tid, 0);
            if (rv == -1 && errno == ESRCH) {
                trace("Thread is done exiting, proceeding with cleanup");
                break;
            } else if (rv != 0) {
                error("Unexpected tgkill rv:%d errno:%s", rv, g_strerror(errno));
                break;
            } else if (pid == tid) {
                trace("%d.%d can still receive signals", pid, tid);

                // Thread leader could be in a zombie state waiting for the other threads to exit.
                gchar* filename = g_strdup_printf("/proc/%d/stat", pid);
                gchar* contents = NULL;
                gboolean rv = g_file_get_contents(filename, &contents, NULL, NULL);
                g_free(filename);
                if (!rv) {
                    trace("tgl %d is fully dead", pid);
                    break;
                }
                bool is_zombie = strstr(contents, ") Z") != NULL;
                g_free(contents);
                if (is_zombie) {
                    trace("tgl %d is a zombie", pid);
                    break;
                }
                // Still alive and in a non-zombie state; continue
            }
            debug("%d.%d still running; waiting for it to exit", pid, tid);
            sched_yield();
            // Check again
        }

        pid_t* clear_child_tid =
            process_getWriteablePtr(process, clear_child_tid_pvp, sizeof(pid_t*));
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
            utility_panic("Couldn't clear child tid; See code comments.");
            abort();
        }

        *clear_child_tid = 0;

        // *don't* flush here. The write may not succeed if the current thread
        // is dead. Leave it pending for the next thread in the process to
        // flush.

        FutexTable* ftable = host_getFutexTable(_host(process));
        utility_debugAssert(ftable);
        Futex* futex =
            futextable_get(ftable, process_getPhysicalAddress(process, clear_child_tid_pvp));
        if (futex) {
            futex_wake(futex, 1);
        }
    }
}

static void _process_terminate(Process* proc) {
    if (!process_hasStarted(proc)) {
        trace("Never started");
        return;
    }

    if (!process_isRunning(proc)) {
        trace("Already dead");
        // We should have already cleaned up.
        utility_debugAssert(_process_didLogReturnCode(proc->rustProcess));
        return;
    }
    trace("Terminating");
    _process_setWasKilledByShadow(proc->rustProcess);

    const pid_t nativePid = _process_getNativePid(proc->rustProcess);
    if (kill(nativePid, SIGKILL)) {
        warning("kill(pid=%d) error %d: %s", nativePid, errno, g_strerror(errno));
    }
    process_markAsExiting(proc);
    _process_handleProcessExit(proc->rustProcess);
}

static void _process_getAndLogReturnCode(Process* proc) {
    if (_process_didLogReturnCode(proc->rustProcess)) {
        return;
    }

    if (!process_hasStarted(proc)) {
        error("Process '%s' with a start time of %" G_GUINT64_FORMAT " did not start",
              process_getName(proc),
              emutime_sub_emutime(
                  _process_getStartTime(proc->rustProcess), EMUTIME_SIMULATION_START));
        return;
    }

    // Return an error if we can't get real exit code.
    int returnCode = EXIT_FAILURE;

    int wstatus = 0;
    const pid_t nativePid = _process_getNativePid(proc->rustProcess);
    int rv = waitpid(nativePid, &wstatus, __WALL);
    if (rv < 0) {
        // Getting here is a bug, but since the process is exiting anyway
        // not serious enough to merit `error`ing out.
        warning("waitpid: %s", g_strerror(errno));
    } else if (rv != nativePid) {
        warning("waitpid returned %d instead of the requested %d", rv, nativePid);
    } else {
        if (WIFEXITED(wstatus)) {
            returnCode = WEXITSTATUS(wstatus);
        } else if (WIFSIGNALED(wstatus)) {
            returnCode = return_code_for_signal(WTERMSIG(wstatus));
        } else {
            warning("Couldn't get exit status");
        }
    }

    _process_setReturnCode(proc->rustProcess, returnCode);

    GString* mainResultString = g_string_new(NULL);
    g_string_printf(mainResultString, "process '%s'", process_getName(proc));
    if (_process_wasKilledByShadow(proc->rustProcess)) {
        g_string_append_printf(mainResultString, " killed by Shadow");
    } else {
        g_string_append_printf(mainResultString, " exited with code %d", returnCode);
        if (returnCode == 0) {
            g_string_append_printf(mainResultString, " (success)");
        } else {
            g_string_append_printf(mainResultString, " (error)");
        }
    }

    char fileName[4000];
    _process_outputFileName(
        proc->rustProcess, _host(proc), "exitcode", &fileName[0], sizeof(fileName));
    FILE* exitcodeFile = fopen(fileName, "we");

    if (exitcodeFile != NULL) {
        if (_process_wasKilledByShadow(proc->rustProcess)) {
            // Process never died during the simulation; shadow chose to kill it;
            // typically because the simulation end time was reached.
            // Write out an empty exitcode file.
        } else {
            fprintf(exitcodeFile, "%d", returnCode);
        }
        fclose(exitcodeFile);
    } else {
        warning("Could not open '%s' for writing: %s", mainResultString->str, strerror(errno));
    }

    // if there was no error or was intentionally killed
    // TODO: once we've implemented clean shutdown via SIGTERM,
    //       treat death by SIGKILL as a plugin error
    if (returnCode == 0 || _process_wasKilledByShadow(proc->rustProcess)) {
        info("%s", mainResultString->str);
    } else {
        warning("%s", mainResultString->str);
        worker_incrementPluginErrors();
    }

    g_string_free(mainResultString, TRUE);
}

pid_t process_findNativeTID(Process* proc, pid_t virtualPID, pid_t virtualTID) {
    MAGIC_ASSERT(proc);

    Thread* thread = NULL;

    pid_t pid = process_getProcessID(proc);
    if (virtualPID > 0 && virtualTID > 0) {
        // Both PID and TID must match
        if (pid == virtualPID) {
            thread = _process_getThread(proc->rustProcess, virtualTID);
        }
    } else if (virtualPID > 0) {
        // Get the TID of the main thread if the PID matches
        if (pid == virtualPID) {
            thread = _process_threadLeader(proc->rustProcess);
        }
    } else if (virtualTID > 0) {
        // Get the TID of any thread that matches, ignoring PID
        thread = _process_getThread(proc->rustProcess, virtualTID);
    }

    if (thread != NULL) {
        return thread_getNativeTid(thread);
    } else {
        return 0; // not found
    }
}

void process_check(Process* proc) {
    MAGIC_ASSERT(proc);

    if (process_isRunning(proc) || !process_hasStarted(proc)) {
        return;
    }

    info("process '%s' has completed or is otherwise no longer running", process_getName(proc));
    _process_getAndLogReturnCode(proc);
#ifdef USE_PERF_TIMERS
    info("total runtime for process '%s' was %f seconds", process_getName(proc),
         _process_getTotalRunTime(proc->rustProcess));
#endif

    utility_alwaysAssert(proc->rustProcess);
    _process_descriptorTableShutdownHelper(proc->rustProcess);
    _process_descriptorTableRemoveAndCloseAll(proc->rustProcess);
}

static void _process_check_thread(Process* proc, Thread* thread) {
    if (thread_isRunning(thread)) {
        debug("thread %d in process '%s' still running, but blocked", thread_getID(thread),
              process_getName(proc));
        return;
    }
    int returnCode = thread_getReturnCode(thread);
    debug("thread %d in process '%s' exited with code %d", thread_getID(thread),
          process_getName(proc), returnCode);
    process_reapThread(proc, thread);
    _process_removeThread(proc->rustProcess, thread_getID(thread));
    process_check(proc);
}

void process_start(Process* proc, const char* const* argv, const char* const* envv_in) {
    MAGIC_ASSERT(proc);

    /* we shouldn't already be running */
    utility_alwaysAssert(!process_isRunning(proc));

    // tid of first thread of a process is equal to the pid.
    int tid = process_getProcessID(proc);
    Thread* mainThread = thread_new(_host(proc), proc, tid);

    _process_insertThread(proc->rustProcess, mainThread);

    // The rust process now owns `mainThread`; drop our own reference.
    thread_unref(mainThread);

    info("starting process '%s'", process_getName(proc));

    /* now we will execute in the pth/plugin context, so we need to load the state */
    worker_setActiveProcess(proc);
    worker_setActiveThread(mainThread);

#ifdef USE_PERF_TIMERS
    /* time how long we execute the program */
    _process_startCpuDelayTimer(proc->rustProcess);
#endif

    gchar** envv = g_strdupv((gchar**)envv_in);

    /* Add shared mem block of first thread to env */
    {
        ShMemBlockSerialized sharedMemBlockSerial =
            shmemallocator_globalBlockSerialize(thread_getShMBlock(mainThread));

        char sharedMemBlockBuf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
        shmemblockserialized_toString(&sharedMemBlockSerial, sharedMemBlockBuf);

        /* append to the env */
        envv = g_environ_setenv(envv, "SHADOW_SHM_THREAD_BLK", sharedMemBlockBuf, TRUE);
    }

    _process_setSharedTime();
    /* exec the process */
    thread_run(mainThread, _process_getPluginPath(proc->rustProcess), argv,
               (const char* const*)envv, process_getWorkingDir(proc), process_getStraceFd(proc));
    g_strfreev(envv);
    const pid_t nativePid = thread_getNativePid(mainThread);
    _process_setNativePid(proc->rustProcess, nativePid);
    _process_createMemoryManager(proc->rustProcess, nativePid);

#ifdef USE_PERF_TIMERS
    gdouble elapsed = _process_stopCpuDelayTimer(proc->rustProcess);
    info("process '%s' started in %f seconds", process_getName(proc), elapsed);
#else
    info("process '%s' started", process_getName(proc));
#endif

    worker_setActiveProcess(NULL);
    worker_setActiveThread(NULL);

    if (_process_shouldPauseForDebugging(proc->rustProcess)) {
        // will block until logger output has been flushed
        // there is a race condition where other threads may log between the fprintf() and raise()
        // below, but it should be rare
        logger_flush(logger_getDefault());

        // print with hopefully a single syscall to avoid splitting these messages (fprintf should
        // not buffer stderr output)
        fprintf(stderr,
                "** Pausing with SIGTSTP to enable debugger attachment to managed process '%s' "
                "(pid %d)\n"
                "** If running Shadow under Bash, resume Shadow by pressing Ctrl-Z to background "
                "this task and then typing \"fg\".\n"
                "** (If you wish to kill Shadow, type \"kill %%%%\" instead.)\n"
                "** If running Shadow under GDB, resume Shadow by typing \"signal SIGCONT\".\n",
                process_getName(proc), nativePid);

        raise(SIGTSTP);
    }

    /* call main and run until blocked */
    process_continue(proc, mainThread);
}

static void _start_thread_task(const Host* host, gpointer callbackObject,
                               gpointer callbackArgument) {
    pid_t pid = GPOINTER_TO_INT(callbackObject);
    Process* process = host_getProcess(host, pid);
    if (!process) {
        debug("Process %d no longer exists", pid);
        return;
    }

    Thread* thread = callbackArgument;
    process_continue(process, thread);
}

static void _unref_thread_cb(gpointer data) {
    Thread* thread = data;
    thread_unref(thread);
}

void process_addThread(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);
    _process_insertThread(proc->rustProcess, thread);

    // Schedule thread to start. We're giving the caller's reference to thread
    // to the TaskRef here, which is why we don't increment its ref count to
    // create the TaskRef, but do decrement it on cleanup.
    const Host* host = _host(proc);
    TaskRef* task = taskref_new_bound(host_getID(host), _start_thread_task,
                                      GINT_TO_POINTER(process_getProcessID(proc)), thread, NULL,
                                      _unref_thread_cb);
    host_scheduleTaskWithDelay(host, task, 0);
    taskref_drop(task);
}

Thread* process_getThread(Process* proc, pid_t virtualTID) {
    return _process_getThread(proc->rustProcess, virtualTID);
}

void process_markAsExiting(Process* proc) {
    MAGIC_ASSERT(proc);
    _process_markAsExiting(proc->rustProcess);
}

void process_continue(Process* proc, Thread* thread) {
    MAGIC_ASSERT(proc);
    trace("Continuing thread %d in process %d", thread_getID(thread), process_getProcessID(proc));

    /* if we are not running, no need to notify anyone */
    if(!process_isRunning(proc)) {
        return;
    }

    debug(
        "switching to thread controller to continue executing process '%s'", process_getName(proc));

    worker_setActiveProcess(proc);
    worker_setActiveThread(thread);

#ifdef USE_PERF_TIMERS
    /* time how long we execute the program */
    _process_startCpuDelayTimer(proc->rustProcess);
#endif

    _process_setSharedTime();

    shimshmem_resetUnappliedCpuLatency(host_getShimShmemLock(_host(proc)));
    thread_resume(thread);

#ifdef USE_PERF_TIMERS
    gdouble elapsed = _process_stopCpuDelayTimer(proc->rustProcess);
    info("process '%s' ran for %f seconds", process_getName(proc), elapsed);
#else
    debug("process '%s' done continuing", process_getName(proc));
#endif

    if (_process_isExiting(proc->rustProcess)) {
        // If the whole process is already exiting, skip to cleaning up the
        // whole process exit; normal thread cleanup would likely fail.
        _process_handleProcessExit(proc->rustProcess);
    } else {
        _process_check_thread(proc, thread);
    }

    worker_setActiveProcess(NULL);
    worker_setActiveThread(NULL);
}

void process_stop(Process* proc) {
    MAGIC_ASSERT(proc);

    info("terminating process '%s'", process_getName(proc));

    worker_setActiveProcess(proc);

#ifdef USE_PERF_TIMERS
    /* time how long we execute the program */
    _process_startCpuDelayTimer(proc->rustProcess);
#endif

    _process_terminate(proc);

#ifdef USE_PERF_TIMERS
    gdouble elapsed = _process_stopCpuDelayTimer(proc->rustProcess);
    info("process '%s' stopped in %f seconds", process_getName(proc), elapsed);
#else
    info("process '%s' stopped", process_getName(proc));
#endif

    worker_setActiveProcess(NULL);

    process_check(proc);
}

void process_detachPlugin(gpointer procptr, gpointer nothing) {
    // TODO: Remove
}

gboolean process_hasStarted(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_hasStarted(proc->rustProcess);
}

gboolean process_isRunning(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_isRunning(proc->rustProcess);
}

void process_initSiginfoForAlarm(siginfo_t* siginfo, int overrun) {
    *siginfo = (siginfo_t){
        .si_signo = SIGALRM,
        .si_code = SI_TIMER,
        .si_overrun = overrun,
    };
}

Process* process_new(const RustProcess* rustProcess, const Host* host, pid_t processID,
                     bool pause_for_debugging) {
    Process* proc = g_new0(Process, 1);
    MAGIC_INIT(proc);

    proc->rustProcess = rustProcess;

    worker_count_allocation(Process);

    return proc;
}

void process_setRustProcess(Process* proc, const RustProcess* rproc) {
    MAGIC_ASSERT(proc);
    utility_alwaysAssert(proc->rustProcess == NULL);
    proc->rustProcess = rproc;
}

const RustProcess* process_getRustProcess(Process* proc) {
    MAGIC_ASSERT(proc);
    utility_alwaysAssert(proc->rustProcess);
    return proc->rustProcess;
}

void process_free(Process* proc) {
    MAGIC_ASSERT(proc);

    // FIXME: call to _process_terminate removed.
    // We can't call it here, since the Rust Process inside the RustProcess (RootededRefCell<Process>)
    // has been extracted, invalidating proc->rustProcess.
    //
    // We *shouldn't* need to call _process_terminate here, since Host::free_all_applications
    // already explicitly stops all processes before freeing them, but once the relevant code is
    // all in Rust we should ensure the process is terminated in our Drop implementation.

    worker_count_deallocation(Process);

    MAGIC_CLEAR(proc);
    g_free(proc);
}

HostId process_getHostId(const Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_getHostId(proc->rustProcess);
}

PluginPhysicalPtr process_getPhysicalAddress(Process* proc, PluginVirtualPtr vPtr) {
    MAGIC_ASSERT(proc);
    return _process_getPhysicalAddress(proc->rustProcess, vPtr);
}

int process_readPtr(Process* proc, void* dst, PluginVirtualPtr src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_readPtr(proc->rustProcess, dst, src, n);
}

int process_writePtr(Process* proc, PluginVirtualPtr dst, const void* src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_writePtr(proc->rustProcess, dst, src, n);
}

const void* process_getReadablePtr(Process* proc, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_getReadablePtr(proc->rustProcess, plugin_src, n);
}

int process_getReadableString(Process* proc, PluginPtr plugin_src, size_t n, const char** out_str,
                              size_t* out_strlen) {
    MAGIC_ASSERT(proc);
    return _process_getReadableString(proc->rustProcess, plugin_src, n, out_str, out_strlen);
}

ssize_t process_readString(Process* proc, char* str, PluginVirtualPtr src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_readString(proc->rustProcess, src, str, n);
}

// Returns a writable pointer corresponding to the named region. The initial
// contents of the returned memory are unspecified.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getWriteablePtr(Process* proc, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_getWriteablePtr(proc->rustProcess, plugin_src, n);
}

// Returns a writeable pointer corresponding to the specified src. Use when
// the data at the given address needs to be both read and written.
//
// The returned pointer is automatically invalidated when the plugin runs again.
void* process_getMutablePtr(Process* proc, PluginPtr plugin_src, size_t n) {
    MAGIC_ASSERT(proc);
    return _process_getMutablePtr(proc->rustProcess, plugin_src, n);
}

// Flushes and invalidates all previously returned readable/writeable plugin
// pointers, as if returning control to the plugin. This can be useful in
// conjunction with `thread_nativeSyscall` operations that touch memory.
int process_flushPtrs(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_flushPtrs(proc->rustProcess);
}

void process_freePtrsWithoutFlushing(Process* proc) {
    MAGIC_ASSERT(proc);
    return _process_freePtrsWithoutFlushing(proc->rustProcess);
}

// ******************************************************
// Handle the descriptors owned by this process
// ******************************************************

bool process_parseArgStr(const char* commandLine, int* argc, char*** argv, char** error) {
    GError* gError = NULL;

    bool rv = !!g_shell_parse_argv(commandLine, argc, argv, &gError);
    if (!rv && gError != NULL && gError->message != NULL && error != NULL) {
        *error = strdup(gError->message);
    }

    if (gError != NULL) {
        g_error_free(gError);
    }
    return rv;
}

void process_parseArgStrFree(char** argv, char* error) {
    if (argv != NULL) {
        g_strfreev(argv);
    }
    if (error != NULL) {
        g_free(error);
    }
}

void process_signal(Process* process, Thread* currentRunningThread, const siginfo_t* siginfo) {
    MAGIC_ASSERT(process);
    utility_debugAssert(siginfo->si_signo >= 0);
    utility_debugAssert(siginfo->si_signo <= SHD_SIGRT_MAX);
    utility_debugAssert(siginfo->si_signo <= SHD_STANDARD_SIGNAL_MAX_NO);

    if (siginfo->si_signo == 0) {
        return;
    }

    const Host* host = _host(process);

    struct shd_kernel_sigaction action = shimshmem_getSignalAction(
        host_getShimShmemLock(host), process_getSharedMem(process), siginfo->si_signo);
    if (action.u.ksa_handler == SIG_IGN ||
        (action.u.ksa_handler == SIG_DFL &&
         shd_defaultAction(siginfo->si_signo) == SHD_KERNEL_DEFAULT_ACTION_IGN)) {
        // Don't deliver an ignored signal.
        return;
    }

    shd_kernel_sigset_t pending_signals = shimshmem_getProcessPendingSignals(
        host_getShimShmemLock(host), process_getSharedMem(process));

    if (shd_sigismember(&pending_signals, siginfo->si_signo)) {
        // Signal is already pending. From signal(7):In the case where a standard signal is already
        // pending, the siginfo_t structure (see sigaction(2)) associated with  that  signal is not
        // overwritten on arrival of subsequent instances of the same signal.
        return;
    }

    shd_sigaddset(&pending_signals, siginfo->si_signo);
    shimshmem_setProcessPendingSignals(
        host_getShimShmemLock(host), process_getSharedMem(process), pending_signals);
    shimshmem_setProcessSiginfo(
        host_getShimShmemLock(host), process_getSharedMem(process), siginfo->si_signo, siginfo);

    if (currentRunningThread != NULL && thread_getProcess(currentRunningThread) == process) {
        shd_kernel_sigset_t blocked_signals = shimshmem_getBlockedSignals(
            host_getShimShmemLock(host), thread_sharedMem(currentRunningThread));
        if (!shd_sigismember(&blocked_signals, siginfo->si_signo)) {
            // Target process is this process, and current thread hasn't blocked
            // the signal.  It will be delivered to this thread when it resumes.
            return;
        }
    }

    _process_interruptWithSignal(process->rustProcess, host_getShimShmemLock(host), siginfo->si_signo);
}

int process_getDumpable(Process* process) {
    return _process_getDumpable(process->rustProcess);
}

void process_setDumpable(Process* process, int dumpable) {
    _process_setDumpable(process->rustProcess, dumpable);
}
