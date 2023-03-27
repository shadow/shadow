#include "main/host/managed_thread.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <inttypes.h>
#include <sched.h>
#include <search.h>
#include <spawn.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/core/worker.h"
#include "main/host/affinity.h"
#include "main/host/syscall_condition.h"

struct _ManagedThread {
    pid_t threadId;
    pid_t processId;
    HostId hostId;

    ShMemBlock ipc_blk;

    int isRunning;
    int returnCode;

    /* holds the event id for the most recent call from the plugin/shim */
    ShimEvent currentEvent;

    /* Typed pointer to ipc_blk.p */
    struct IPCData* ipc_data;

    uint64_t notificationHandle;

    pid_t nativePid;
    pid_t nativeTid;

    // Value storing the current CPU affinity of the thread (more preceisely,
    // of the native thread backing this thread object). This value will be set
    // to AFFINITY_UNINIT if CPU pinning is not enabled or if the thread has
    // not yet been pinned to a CPU.
    int affinity;
};

typedef struct _ShMemWriteBlock {
    ShMemBlock blk;
    PluginPtr plugin_ptr;
    size_t n;
} ShMemWriteBlock;

static const Thread* _mthread_getThread(const ManagedThread* t) {
    const Thread* thread = worker_getCurrentThread();
    utility_debugAssert(thread_getID(thread) == t->threadId);
    return thread;
}

static const ProcessRefCell* _mthread_getProcess(const ManagedThread* t) {
    const ProcessRefCell* process = worker_getCurrentProcess();
    utility_debugAssert(process_getProcessID(process) == t->processId);
    return process;
}

static const Host* _mthread_getHost(const ManagedThread* t) {
    const Host* host = worker_getCurrentHost();
    utility_debugAssert(host_getID(host) == t->hostId);
    return host;
}

/*
 * Helper function. Sets the thread's CPU affinity to the worker's affinity.
 */
static void _managedthread_syncAffinityWithWorker(ManagedThread* mthread) {
    int current_affinity = scheduler_getAffinity();
    if (current_affinity < 0) {
        current_affinity = AFFINITY_UNINIT;
    }
    mthread->affinity =
        affinity_setProcessAffinity(mthread->nativeTid, current_affinity, mthread->affinity);
}

static void _managedthread_continuePlugin(ManagedThread* thread, const ShimEvent* event) {
    // We're about to let managed thread execute, so need to release the shared memory lock.
    const Host* host = _mthread_getHost(thread);
    shimshmem_setMaxRunaheadTime(
        host_getShimShmemLock(host), worker_maxEventRunaheadTime(host));
    shimshmem_setEmulatedTime(
        host_getSharedMem(host), worker_getCurrentEmulatedTime());

    // Reacquired in _managedthread_waitForNextEvent.
    host_unlockShimShmemLock(host);

    shimevent_sendEventToPlugin(thread->ipc_data, event);
}

void managedthread_free(ManagedThread* mthread) {
    if (mthread->notificationHandle) {
        childpidwatcher_unwatch(
            worker_getChildPidWatcher(), mthread->nativePid, mthread->notificationHandle);
        mthread->notificationHandle = 0;
    }

    if (mthread->ipc_data) {
        ipcData_destroy(mthread->ipc_data);
        mthread->ipc_data = NULL;
    }

    if (mthread->ipc_blk.p) {
        // FIXME: This appears to cause errors.
        // shmemallocator_globalFree(&thread->ipc_blk);
    }

    worker_count_deallocation(ManagedThread);
}

static gchar** _add_u64_to_env(gchar** envp, const char* var, uint64_t x) {
    enum { BUF_NBYTES = 256 };
    char strbuf[BUF_NBYTES] = {0};

    snprintf(strbuf, BUF_NBYTES, "%" PRIu64, x);

    envp = g_environ_setenv(envp, var, strbuf, TRUE);

    return envp;
}

static pid_t _managedthread_spawn(ManagedThread* thread, const char* file,
                                  const char* const* argv_in, const char* const* envp_in,
                                  const char* workingDir, int straceFd, int shimlogFd) {
    // We use `posix_spawn` to spawn the child process. This should be functionally
    // equivalent to vfork + execve, but wrapped in a safer and more portable API.
    utility_debugAssert(file != NULL);

    // posix_spawn technically takes arrays of pointers to *mutable* char.
    // conservatively dup here.
    gchar** argv = g_strdupv((gchar**)argv_in);
    gchar** envp = g_strdupv((gchar**)envp_in);

    // Tell the shim to change the working dir.
    //
    // TODO: Instead use posix_spawn_file_actions_addchdir_np, which was added
    // in glibc 2.29. We should be able to do so once we've dropped support
    // for some platforms, as planned for the shadow 3.0 release.
    // https://github.com/shadow/shadow/discussions/2496
    envp = g_environ_setenv(envp, "SHADOW_WORKING_DIR", workingDir, TRUE);

    // For childpidwatcher. We must create them O_CLOEXEC to prevent them from
    // "leaking" into a concurrently forked child.
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC)) {
        utility_panic("pipe2: %s", g_strerror(errno));
    }

    posix_spawn_file_actions_t file_actions;
    if (posix_spawn_file_actions_init(&file_actions) != 0) {
        utility_panic("posix_spawn_file_actions_init: %s", g_strerror(errno));
    }

    // Dup the write end of the pipe; the dup'd descriptor won't have O_CLOEXEC set.
    //
    // Since dup2 is a no-op when the new and old file descriptors are equal, we have
    // to arrange to call dup2 twice - first to a temporary descriptor, and then back
    // to the original descriptor number.
    //
    // Here we use STDOUT_FILENO as the temporary descriptor, since we later
    // replace that below.
    //
    // Once we drop support for platforms with glibc older than 2.29, we *could*
    // consider taking advantage of a new feature that would let us just use a
    // single `posix_spawn_file_actions_adddup2` call with equal descriptors.
    // OTOH it's a non-standard extension, and I think ultimately uses the same
    // number of syscalls, so it might be better to continue using this slightly
    // more awkward method anyway.
    // https://github.com/bminor/glibc/commit/805334b26c7e6e83557234f2008497c72176a6cd
    // https://austingroupbugs.net/view.php?id=411
    if (posix_spawn_file_actions_adddup2(&file_actions, pipefd[1], STDOUT_FILENO) != 0) {
        utility_panic("posix_spawn_file_actions_adddup2: %s", g_strerror(errno));
    }
    if (posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO, pipefd[1]) != 0) {
        utility_panic("posix_spawn_file_actions_adddup2: %s", g_strerror(errno));
    }

    // Likewise for straceFd.
    if (straceFd >= 0) {
        if (posix_spawn_file_actions_adddup2(&file_actions, straceFd, STDOUT_FILENO) != 0) {
            utility_panic("posix_spawn_file_actions_adddup2: %s", g_strerror(errno));
        }
        if (posix_spawn_file_actions_adddup2(&file_actions, STDOUT_FILENO, straceFd) != 0) {
            utility_panic("posix_spawn_file_actions_adddup2: %s", g_strerror(errno));
        }
    }

    // set stdout/stderr as the shim log, and clear the FD_CLOEXEC flag so that it's
    // available in the shim
    if (posix_spawn_file_actions_adddup2(&file_actions, shimlogFd, STDOUT_FILENO) != 0) {
        utility_panic("posix_spawn_file_actions_adddup2: %s", g_strerror(errno));
    }
    if (posix_spawn_file_actions_adddup2(&file_actions, shimlogFd, STDERR_FILENO) != 0) {
        utility_panic("posix_spawn_file_actions_adddup2: %s", g_strerror(errno));
    }

    posix_spawnattr_t attr;
    if (posix_spawnattr_init(&attr) != 0) {
        utility_panic("posix_spawnattr_init: %s", g_strerror(errno));
    }

    // In versions of glibc before 2.24, we need this to tell posix_spawn
    // to use vfork instead of fork. In later versions it's a no-op.
    if (posix_spawnattr_setflags(&attr, POSIX_SPAWN_USEVFORK) != 0) {
        utility_panic("posix_spawnattr_setflags: %s", g_strerror(errno));
    }

    pid_t pid;
    if (posix_spawn(&pid, file, &file_actions, &attr, argv, envp) != 0) {
        utility_panic("posix_spawn: %s", g_strerror(errno));
    }

    if (posix_spawn_file_actions_destroy(&file_actions) != 0) {
        utility_panic("posix_spawn_file_actions: %s", g_strerror(errno));
    }

    if (posix_spawnattr_destroy(&attr) != 0) {
        utility_panic("posix_spawnattr_destroy: %s", g_strerror(errno));
    }

    // *Must* close the write-end of the pipe, so that the child's copy is the
    // last remaining one, allowing the read-end to be notified when the child
    // exits.
    if (close(pipefd[1])) {
        utility_panic("close: %s", g_strerror(errno));
    }

    childpidwatcher_registerPid(worker_getChildPidWatcher(), pid, pipefd[0]);

    g_strfreev(argv);
    g_strfreev(envp);

    debug("started process %s with PID %d", file, pid);
    return pid;
}

static void _managedthread_cleanup(ManagedThread* thread) {
    trace("child %d exited", thread->nativePid);
    thread->isRunning = 0;
}

static void _markPluginExited(pid_t pid, void* voidIPC) {
    struct IPCData* ipc = voidIPC;
    ipcData_markPluginExited(ipc);
}

void managedthread_run(ManagedThread* mthread, const char* pluginPath, const char* const* argv,
                       const char* const* envv, const char* workingDir, int straceFd,
                       const char* logPath) {
    _managedthread_syncAffinityWithWorker(mthread);

    /* set the env for the child */
    gchar** myenvv = g_strdupv((gchar**)envv);

    mthread->ipc_blk = shmemallocator_globalAlloc(ipcData_nbytes());
    utility_debugAssert(mthread->ipc_blk.p);
    mthread->ipc_data = mthread->ipc_blk.p;
    ipcData_init(mthread->ipc_data);

    ShMemBlockSerialized ipc_blk_serial = shmemallocator_globalBlockSerialize(&mthread->ipc_blk);

    char ipc_blk_buf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
    shmemblockserialized_toString(&ipc_blk_serial, ipc_blk_buf);

    /* append to the env */
    myenvv = g_environ_setenv(myenvv, "SHADOW_IPC_BLK", ipc_blk_buf, TRUE);

    // set shadow's PID in the env so the child can run get_ppid
    myenvv = _add_u64_to_env(myenvv, "SHADOW_PID", getpid());

    // Pass the TSC Hz to the shim, so that it can emulate rdtsc.
    myenvv = _add_u64_to_env(
        myenvv, "SHADOW_TSC_HZ", host_getTsc(_mthread_getHost(mthread))->cyclesPerSecond);

    gchar* envStr = utility_strvToNewStr(myenvv);
    gchar* argStr = utility_strvToNewStr((gchar**)argv);
    info("forking new mthread with environment '%s', arguments '%s', and working directory '%s'",
         envStr, argStr, workingDir);
    g_free(envStr);
    g_free(argStr);

    int shimlogFd = open(
        logPath, O_WRONLY | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH | S_IWOTH);
    utility_alwaysAssert(shimlogFd >= 0);

    mthread->nativePid = _managedthread_spawn(
        mthread, pluginPath, argv, (const char* const*)myenvv, workingDir, straceFd, shimlogFd);

    // should be opened in the shim, so no need for it anymore
    close(shimlogFd);

    // In Linux, the PID is equal to the TID of its first thread.
    mthread->nativeTid = mthread->nativePid;
    mthread->notificationHandle = childpidwatcher_watch(
        worker_getChildPidWatcher(), mthread->nativePid, _markPluginExited, mthread->ipc_data);

    /* cleanup the dupd env*/
    if (myenvv) {
        g_strfreev(myenvv);
    }

    // TODO get to the point where the plugin blocks before calling main()
    shimevent_initStart(&mthread->currentEvent);

    /* mthread is now active */
    mthread->isRunning = 1;
}

static inline void _managedthread_waitForNextEvent(ManagedThread* mthread, ShimEvent* e) {
    utility_debugAssert(mthread->ipc_data);
    shimevent_recvEventFromPlugin(mthread->ipc_data, e);
    // The managed mthread has yielded control back to us. Reacquire the shared
    // memory lock, which we released in `_managedthread_continuePlugin`.
    host_lockShimShmemLock(_mthread_getHost(mthread));
    trace("received shim_event %d", shimevent_getId(&mthread->currentEvent));

    // Update time, which may have been incremented in the shim.
    CEmulatedTime shimTime =
        shimshmem_getEmulatedTime(host_getSharedMem(_mthread_getHost(mthread)));
    if (shimTime != worker_getCurrentEmulatedTime()) {
        trace("Updating time from %ld to %ld (+%ld)", worker_getCurrentEmulatedTime(), shimTime,
              shimTime - worker_getCurrentEmulatedTime());
    }
    worker_setCurrentEmulatedTime(shimTime);
}

ShMemBlock* managedthread_getIPCBlock(ManagedThread* mthread) { return &mthread->ipc_blk; }

SysCallCondition* managedthread_resume(ManagedThread* mthread) {
    utility_debugAssert(mthread->isRunning);
    utility_debugAssert(shimevent_getId(&mthread->currentEvent) != SHIM_EVENT_NULL);

    const Thread* thread = _mthread_getThread(mthread);
    const ProcessRefCell* process = _mthread_getProcess(mthread);

    _managedthread_syncAffinityWithWorker(mthread);

    // Flush any pending writes, e.g. from a previous mthread that exited without flushing.
    {
        int res = process_flushPtrs(_mthread_getProcess(mthread));
        if (res) {
            panic("Couldn't flush cached memory reference: %s", g_strerror(-res));
        }
    }

    while (true) {
        switch (shimevent_getId(&mthread->currentEvent)) {
            case SHIM_EVENT_START: {
                // send the message to the shim to call main(),
                // the plugin will run until it makes a blocking call
                trace(
                    "sending start event code to %d on %p", mthread->nativePid, mthread->ipc_data);

                _managedthread_continuePlugin(mthread, &mthread->currentEvent);
                break;
            }
            case SHIM_EVENT_PROCESS_DEATH: {
                // The native threads are all dead or zombies. Nothing to do but
                // clean up.
                process_markAsExiting(process);
                _managedthread_cleanup(mthread);

                // it will not be sending us any more events
                return NULL;
            }
            case SHIM_EVENT_SYSCALL: {
                // `exit` is tricky since it only exits the *mthread*, and we don't have a way
                // to be notified that the mthread has exited. We have to "fire and forget"
                // the command to execute the syscall natively.
                const ShimEventSyscall* syscall = shimevent_getSyscallData(&mthread->currentEvent);
                if (syscall->syscall_args.number == SYS_exit) {
                    // Tell mthread to go ahead and make the exit syscall itself.
                    // We *don't* call `_managedthread_continuePlugin` here,
                    // since that'd release the ShimSharedMemHostLock, and we
                    // aren't going to get a message back to know when it'd be
                    // safe to take it again.
                    ShimEvent ev;
                    shimevent_initSyscallDoNative(&ev);
                    shimevent_sendEventToPlugin(mthread->ipc_data, &ev);
                    // Clean up the mthread.
                    _managedthread_cleanup(mthread);
                    return NULL;
                }

                SyscallReturn result = syscallhandler_make_syscall(
                    thread_getSysCallHandler(thread), &syscall->syscall_args);

                // remove the mthread's old syscall condition since it's no longer needed
                thread_clearSysCallCondition(thread);

                if (!mthread->isRunning) {
                    return NULL;
                }

                // Flush any writes the syscallhandler made.
                {
                    int res = process_flushPtrs(process);
                    if (res) {
                        panic(
                            "Couldn't flush syscallhandler memory reference: %s", g_strerror(-res));
                    }
                }

                if (result.tag == SYSCALL_RETURN_BLOCK) {
                    return syscallreturn_blocked(&result)->cond;
                }

                ShimEvent shim_result;
                if (result.tag == SYSCALL_RETURN_DONE) {
                    // Now send the result of the syscall
                    SyscallReturnDone* done = syscallreturn_done(&result);
                    shimevent_initSysCallComplete(&shim_result, done->retval, done->restartable);
                } else if (result.tag == SYSCALL_RETURN_NATIVE) {
                    // Tell the shim to make the syscall itself
                    shimevent_initSyscallDoNative(&shim_result);
                }
                _managedthread_continuePlugin(mthread, &shim_result);
                break;
            }
            case SHIM_EVENT_SYSCALL_COMPLETE: {
                _managedthread_continuePlugin(mthread, &mthread->currentEvent);
                break;
            }
            default: {
                utility_panic("unknown event type: %d", shimevent_getId(&mthread->currentEvent));
                break;
            }
        }
        utility_debugAssert(mthread->isRunning);

        /* previous event was handled, wait for next one */
        _managedthread_waitForNextEvent(mthread, &mthread->currentEvent);
    }
}

void managedthread_handleProcessExit(ManagedThread* mthread) {
    // TODO [rwails]: come back and make this logic more solid

    childpidwatcher_unregisterPid(worker_getChildPidWatcher(), mthread->nativePid);

    if (!mthread->isRunning) {
        return;
    }

    int status = 0;

    utility_debugAssert(mthread->nativePid > 0);

    _managedthread_cleanup(mthread);
}

int managedthread_getReturnCode(ManagedThread* mthread) { return mthread->returnCode; }

bool managedthread_isRunning(ManagedThread* mthread) { return mthread->isRunning; }

pid_t managedthread_clone(ManagedThread* child, ManagedThread* parent, unsigned long flags,
                          PluginPtr child_stack, PluginPtr ptid, PluginPtr ctid,
                          unsigned long newtls) {
    child->ipc_blk = shmemallocator_globalAlloc(ipcData_nbytes());
    utility_debugAssert(child->ipc_blk.p);
    child->ipc_data = child->ipc_blk.p;
    ipcData_init(child->ipc_data);
    child->notificationHandle = childpidwatcher_watch(
        worker_getChildPidWatcher(), parent->nativePid, _markPluginExited, child->ipc_data);
    ShMemBlockSerialized ipc_blk_serial = shmemallocator_globalBlockSerialize(&child->ipc_blk);

    // Send an IPC block for the new mthread to use.
    ShimEvent ev;
    shimevent_initAddThreadReq(&ev, &ipc_blk_serial);
    _managedthread_continuePlugin(parent, &ev);
    ShimEvent response = {0};
    _managedthread_waitForNextEvent(parent, &response);
    utility_debugAssert(shimevent_getId(&response) == SHIM_EVENT_ADD_THREAD_PARENT_RES);

    // Create the new managed thread.
    pid_t childNativeTid = thread_nativeSyscall(
        _mthread_getThread(parent), SYS_clone, flags, child_stack, ptid, ctid, newtls);
    if (childNativeTid < 0) {
        trace("native clone failed %d(%s)", childNativeTid, strerror(-childNativeTid));
        return childNativeTid;
    }
    trace("native clone created tid %d", childNativeTid);
    child->nativePid = parent->nativePid;
    child->nativeTid = childNativeTid;

    // Child is now ready to start.
    shimevent_initStart(&child->currentEvent);
    child->isRunning = 1;

    return 0;
}

long managedthread_nativeSyscall(ManagedThread* mthread, long n, ...) {
    SysCallArgs syscall_args;
    syscall_args.number = n;
    // We don't know how many arguments there actually are, but the x86_64 linux
    // ABI supports at most 6 arguments, and processing more arguments here than
    // were actually passed doesn't hurt anything. e.g. this is what libc's
    // syscall(2) function does as well.
    va_list(args);
    va_start(args, n);
    for (int i = 0; i < 6; ++i) {
        syscall_args.args[i].as_i64 = va_arg(args, int64_t);
    }
    va_end(args);

    ShimEvent req;
    shimevent_initSyscall(&req, &syscall_args);
    _managedthread_continuePlugin(mthread, &req);

    ShimEvent res;
    _managedthread_waitForNextEvent(mthread, &res);
    ShimEvent_Tag res_id = shimevent_getId(&res);
    if (res_id == SHIM_EVENT_PROCESS_DEATH) {
        trace("Plugin exited while executing native syscall %ld", n);
        process_markAsExiting(_mthread_getProcess(mthread));
        _managedthread_cleanup(mthread);

        // We have to return *something* here. Probably doesn't matter much what.
        return -ESRCH;
    }
    utility_debugAssert(res_id == SHIM_EVENT_SYSCALL_COMPLETE);
    const ShimEventSyscallComplete* syscall_complete = shimevent_getSyscallCompleteData(&res);
    return syscall_complete->retval.as_i64;
}

ManagedThread* managedthread_new(HostId hostId, pid_t processId, pid_t threadId) {
    ManagedThread* mthread = g_new(ManagedThread, 1);
    *mthread = (ManagedThread){
        .hostId = hostId,
        .processId = processId,
        .threadId = threadId,
        .affinity = AFFINITY_UNINIT,
    };

    _Static_assert(sizeof(void*) == 8, "thread-preload impl assumes 8 byte pointers");

    // thread has access to a global, thread safe shared memory manager

    // this function is called when the process is created at the beginning
    // of the sim. but the process may not launch/start until later. any
    // resources for launch/start should be allocated in the respective funcs.

    worker_count_allocation(ManagedThread);
    return mthread;
}

pid_t managedthread_getNativePid(ManagedThread* mthread) { return mthread->nativePid; }

pid_t managedthread_getNativeTid(ManagedThread* mthread) { return mthread->nativeTid; }
