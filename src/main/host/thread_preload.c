#include "main/host/thread_preload.h"

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <sched.h>
#include <search.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/logger/logger.h"
#include "lib/shim/ipc.h"
#include "lib/shim/shim_event.h"
#include "main/core/worker.h"
#include "main/host/shimipc.h"
#include "main/host/syscall_condition.h"
#include "main/host/thread_protected.h"
#include "main/shmem/shmem_allocator.h"

#define THREADPRELOAD_TYPE_ID 13357

struct _ThreadPreload {
    Thread base;

    ShMemBlock ipc_blk;

    int isRunning;
    int returnCode;

    /* holds the event id for the most recent call from the plugin/shim */
    ShimEvent currentEvent;

    /* Typed pointer to ipc_blk.p */
    struct IPCData* ipc_data;

    uint64_t notificationHandle;
};

typedef struct _ShMemWriteBlock {
    ShMemBlock blk;
    PluginPtr plugin_ptr;
    size_t n;
} ShMemWriteBlock;

static ThreadPreload* _threadToThreadPreload(Thread* thread) {
    utility_assert(thread->type_id == THREADPRELOAD_TYPE_ID);
    return (ThreadPreload*)thread;
}

static Thread* _threadPreloadToThread(ThreadPreload* thread) { return (Thread*)thread; }

static void _threadpreload_continuePlugin(ThreadPreload* thread, const ShimEvent* event) {
    // We're about to let managed thread execute, so need to release the shared memory lock.
    // Reacquired in _threadpreload_waitForNextEvent.
    host_unlockShimShmemLock(thread->base.host);

    shimevent_sendEventToPlugin(thread->ipc_data, event);
}

void threadpreload_free(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    if (thread->base.sys) {
        syscallhandler_unref(thread->base.sys);
    }

    if (thread->notificationHandle) {
        childpidwatcher_unwatch(
            worker_getChildPidWatcher(), base->nativePid, thread->notificationHandle);
        thread->notificationHandle = 0;
    }

    if (thread->ipc_data) {
        ipcData_destroy(thread->ipc_data);
        thread->ipc_data = NULL;
    }

    if (thread->ipc_blk.p) {
        // FIXME: This appears to cause errors.
        // shmemallocator_globalFree(&thread->ipc_blk);
    }

    worker_count_deallocation(ThreadPreload);
}

static gchar** _add_u64_to_env(gchar** envp, const char* var, uint64_t x) {
    enum { BUF_NBYTES = 256 };
    char strbuf[BUF_NBYTES] = {0};

    snprintf(strbuf, BUF_NBYTES, "%" PRIu64, x);

    envp = g_environ_setenv(envp, var, strbuf, TRUE);

    return envp;
}

static pid_t _threadpreload_fork_exec(ThreadPreload* thread, const char* file, char* const argv[],
                                      char* const envp[], const char* workingDir) {
    // For childpidwatcher. We must create them O_CLOEXEC to prevent them from
    // "leaking" into a concurrently forked child.
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC)) {
        utility_panic("pipe2: %s", g_strerror(errno));
    }

    // vfork has superior performance to fork with large workloads.
    pid_t pid = vfork();

    // Beware! Unless you really know what you're doing, don't add any code
    // between here and the execvpe below. The forked child process is sharing
    // memory and control structures with the parent at this point. See
    // `man 2 vfork`.

    switch (pid) {
        case -1:
            utility_panic("fork failed");
            return -1;
            break;
        case 0: {
            // child

            // *Don't* close the write end of the pipe on exec.
            if (fcntl(pipefd[1], F_SETFD, 0)) {
                die_after_vfork();
            }

            // Set the working directory
            if (chdir(workingDir) < 0) {
                die_after_vfork();
            }

            int rc = execvpe(file, argv, envp);
            if (rc == -1) {
                die_after_vfork();
            }
            // Unreachable
            die_after_vfork();
        }
        default: // parent
            break;
    }

    // *Must* close the write-end of the pipe, so that the child's copy is the
    // last remaining one, allowing the read-end to be notified when the child
    // exits.
    if (close(pipefd[1])) {
        utility_panic("close: %s", g_strerror(errno));
    }

    childpidwatcher_registerPid(worker_getChildPidWatcher(), pid, pipefd[0]);

    debug("started process %s with PID %d", file, pid);
    return pid;
}

static void _threadpreload_cleanup(ThreadPreload* thread) {
    trace("child %d exited", thread->base.nativePid);
    thread->isRunning = 0;

    if (thread->base.sys) {
        syscallhandler_unref(thread->base.sys);
        thread->base.sys = NULL;
    }
}

static void _markPluginExited(pid_t pid, void* voidIPC) {
    struct IPCData* ipc = voidIPC;
    ipcData_markPluginExited(ipc);
}

pid_t threadpreload_run(Thread* base, gchar** argv, gchar** envv, const char* workingDir) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    /* set the env for the child */
    gchar** myenvv = g_strdupv(envv);

    thread->ipc_blk = shmemallocator_globalAlloc(ipcData_nbytes());
    utility_assert(thread->ipc_blk.p);
    thread->ipc_data = thread->ipc_blk.p;
    ipcData_init(thread->ipc_data, shimipc_spinMax());

    ShMemBlockSerialized ipc_blk_serial = shmemallocator_globalBlockSerialize(&thread->ipc_blk);

    char ipc_blk_buf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
    shmemblockserialized_toString(&ipc_blk_serial, ipc_blk_buf);

    /* append to the env */
    myenvv = g_environ_setenv(myenvv, "SHADOW_IPC_BLK", ipc_blk_buf, TRUE);

    /* Tell the shim in the managed process whether to enable seccomp */
    if (shimipc_getUseSeccomp()) {
        myenvv = g_environ_setenv(myenvv, "SHADOW_USE_SECCOMP", "", TRUE);
    }

    // set shadow's PID in the env so the child can run get_ppid
    myenvv = _add_u64_to_env(myenvv, "SHADOW_PID", getpid());

    // Pass the TSC Hz to the shim, so that it can emulate rdtsc.
    myenvv =
        _add_u64_to_env(myenvv, "SHADOW_TSC_HZ", host_getTsc(thread->base.host)->cyclesPerSecond);

    gchar* envStr = utility_strvToNewStr(myenvv);
    gchar* argStr = utility_strvToNewStr(argv);
    info("forking new thread with environment '%s', arguments '%s', and working directory '%s'",
         envStr, argStr, workingDir);
    g_free(envStr);
    g_free(argStr);

    pid_t child_pid = _threadpreload_fork_exec(thread, argv[0], argv, myenvv, workingDir);
    childpidwatcher_watch(
        worker_getChildPidWatcher(), child_pid, _markPluginExited, thread->ipc_data);

    /* cleanup the dupd env*/
    if (myenvv) {
        g_strfreev(myenvv);
    }

    // TODO get to the point where the plugin blocks before calling main()
    thread->currentEvent.event_id = SHD_SHIM_EVENT_START;

    /* thread is now active */
    thread->isRunning = 1;

    return child_pid;
}

static inline void _threadpreload_waitForNextEvent(ThreadPreload* thread, ShimEvent *e) {
    MAGIC_ASSERT(_threadPreloadToThread(thread));
    utility_assert(thread->ipc_data);
    shimevent_recvEventFromPlugin(thread->ipc_data, e);
    // The managed thread has yielded control back to us. Reacquire the shared
    // memory lock, which we released in `_threadpreload_continuePlugin`.
    host_lockShimShmemLock(thread->base.host);
    trace("received shim_event %d", thread->currentEvent.event_id);
}

static ShMemBlock* _threadpreload_getIPCBlock(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);
    return &thread->ipc_blk;
}

SysCallCondition* threadpreload_resume(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    utility_assert(thread->isRunning);
    utility_assert(thread->currentEvent.event_id != SHD_SHIM_EVENT_NULL);

    // Flush any pending writes, e.g. from a previous thread that exited without flushing.
    process_flushPtrs(thread->base.process);

    while (true) {
        switch (thread->currentEvent.event_id) {
            case SHD_SHIM_EVENT_START: {
                // send the message to the shim to call main(),
                // the plugin will run until it makes a blocking call
                trace("sending start event code to %d on %p", thread->base.nativePid,
                      thread->ipc_data);

                _threadpreload_continuePlugin(thread, &thread->currentEvent);
                break;
            }
            case SHD_SHIM_EVENT_STOP: {
                // the plugin stopped running
                _threadpreload_cleanup(thread);
                // it will not be sending us any more events
                return NULL;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                // `exit` is tricky since it only exits the *thread*, and we don't have a way
                // to be notified that the thread has exited. We have to "fire and forget"
                // the command to execute the syscall natively.
                if (thread->currentEvent.event_data.syscall.syscall_args.number == SYS_exit) {
                    // Tell thread to go ahead and make the exit syscall itself.
                    // We *don't* call `_threadpreload_continuePlugin` here,
                    // since that'd release the ShimSharedMemHostLock, and we
                    // aren't going to get a message back to know when it'd be
                    // safe to take it again.
                    shimevent_sendEventToPlugin(
                        thread->ipc_data,
                        &(ShimEvent){.event_id = SHD_SHIM_EVENT_SYSCALL_DO_NATIVE});
                    // Clean up the thread.
                    _threadpreload_cleanup(thread);
                    return NULL;
                }

                // Some syscall handlers can result in death of the thread,
                // which unrefs the handler in cleanup. We keep an extra
                // reference to it during the call to prevent it from
                // disappearing while it's still referenced on the call stack.
                SysCallHandler* handler = thread->base.sys;
                syscallhandler_ref(handler);
                SysCallReturn result = syscallhandler_make_syscall(
                    handler, &thread->currentEvent.event_data.syscall.syscall_args);
                syscallhandler_unref(handler);
                handler = NULL;

                // remove the thread's old syscall condition since it's no longer needed
                if (thread->base.cond) {
                    syscallcondition_unref(thread->base.cond);
                    thread->base.cond = NULL;
                }

                if (!thread->isRunning) {
                    return NULL;
                }

                // Flush any writes the syscallhandler made.
                process_flushPtrs(thread->base.process);

                if (result.state == SYSCALL_BLOCK) {
                    if (shimipc_sendExplicitBlockMessageEnabled()) {
                        trace("Sending block message to plugin");
                        // thread is blocked on simulation progress. Tell it to
                        // stop spinning so that releases its CPU core for the next
                        // thread to be run.
                        ShimEvent block_event = {.event_id = SHD_SHIM_EVENT_BLOCK};
                        _threadpreload_continuePlugin(thread, &block_event);
                        _threadpreload_waitForNextEvent(thread, &thread->currentEvent);
                    }

                    return result.cond;
                }

                ShimEvent shim_result;
                if (result.state == SYSCALL_DONE) {
                    // Now send the result of the syscall
                    shim_result = (ShimEvent){.event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                                              .event_data = {
                                                  .syscall_complete = {.retval = result.retval},

                                              }};
                } else if (result.state == SYSCALL_NATIVE) {
                    // Tell the shim to make the syscall itself
                    shim_result = (ShimEvent){
                        .event_id = SHD_SHIM_EVENT_SYSCALL_DO_NATIVE,
                    };
                }
                _threadpreload_continuePlugin(thread, &shim_result);
                break;
            }
            case SHD_SHIM_EVENT_SYSCALL_COMPLETE: {
                _threadpreload_continuePlugin(thread, &thread->currentEvent);
                break;
            }
            default: {
                utility_panic("unknown event type: %d");
                break;
            }
        }
        utility_assert(thread->isRunning);

        /* previous event was handled, wait for next one */
        _threadpreload_waitForNextEvent(thread, &thread->currentEvent);
    }
}

void threadpreload_handleProcessExit(Thread* base) {
    MAGIC_ASSERT(base);
    ThreadPreload* thread = _threadToThreadPreload(base);
    // TODO [rwails]: come back and make this logic more solid

    childpidwatcher_unregisterPid(worker_getChildPidWatcher(), base->nativePid);

    /* make sure we cleanup circular refs */
    if (thread->base.sys) {
        syscallhandler_unref(thread->base.sys);
        thread->base.sys = NULL;
    }

    if (!thread->isRunning) {
        return;
    }

    int status = 0;

    utility_assert(thread->base.nativePid > 0);

    _threadpreload_cleanup(thread);
}

int threadpreload_getReturnCode(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);
    return thread->returnCode;
}

bool threadpreload_isRunning(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    // TODO
    // return TRUE if at least one thread is still running
    // return false if the process died or completed
    return thread->isRunning;
}

static int _threadpreload_clone(Thread* base, unsigned long flags, PluginPtr child_stack, PluginPtr ptid,
                       PluginPtr ctid, unsigned long newtls, Thread** childp) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    *childp = threadpreload_new(base->host, base->process, host_getNewProcessID(base->host));
    ThreadPreload* child = _threadToThreadPreload(*childp);
    child->ipc_blk = shmemallocator_globalAlloc(ipcData_nbytes());
    utility_assert(child->ipc_blk.p);
    child->ipc_data = child->ipc_blk.p;
    ipcData_init(child->ipc_data, shimipc_spinMax());
    childpidwatcher_watch(
        worker_getChildPidWatcher(), base->nativePid, _markPluginExited, child->ipc_data);
    ShMemBlockSerialized ipc_blk_serial = shmemallocator_globalBlockSerialize(&child->ipc_blk);

    // Send an IPC block for the new thread to use.
    _threadpreload_continuePlugin(thread, &(ShimEvent){.event_id = SHD_SHIM_EVENT_ADD_THREAD_REQ,
                                                       .event_data.add_thread_req = {
                                                           .ipc_block = ipc_blk_serial,
                                                       }});
    _threadpreload_waitForNextEvent(thread, &thread->currentEvent);
    utility_assert(thread->currentEvent.event_id == SHD_SHIM_EVENT_ADD_THREAD_PARENT_RES);

    // Create the new managed thread.
    pid_t childNativeTid = thread_nativeSyscall(base, SYS_clone, flags, child_stack, ptid, ctid, newtls);
    if (childNativeTid < 0) {
        trace("native clone failed %d(%s)", childNativeTid, strerror(-childNativeTid));
        thread_unref(*childp);
        *childp = NULL;
        return childNativeTid;
    }
    trace("native clone created tid %d", childNativeTid);
    child->base.nativePid = base->nativePid;
    child->base.nativeTid = childNativeTid;
    
    // Child is now ready to start.
    child->currentEvent.event_id = SHD_SHIM_EVENT_START;
    child->isRunning = 1;

    return childNativeTid;
}

long threadpreload_nativeSyscall(Thread* base, long n, va_list args) {
    ThreadPreload* thread = _threadToThreadPreload(base);
    ShimEvent req = {
        .event_id = SHD_SHIM_EVENT_SYSCALL,
        .event_data.syscall.syscall_args.number = n,
    };
    // We don't know how many arguments there actually are, but the x86_64 linux
    // ABI supports at most 6 arguments, and processing more arguments here than
    // were actually passed doesn't hurt anything. e.g. this is what libc's
    // syscall(2) function does as well.
    for (int i = 0; i < 6; ++i) {
        req.event_data.syscall.syscall_args.args[i].as_i64 = va_arg(args, int64_t);
    }
    _threadpreload_continuePlugin(thread, &req);

    ShimEvent res;
    _threadpreload_waitForNextEvent(thread, &res);
    if (res.event_id == SHD_SHIM_EVENT_STOP) {
        trace("Plugin exited while executing native syscall %ld", n);
        _threadpreload_cleanup(thread);
        // We have to return *something* here. Probably doesn't matter much what.
        return -ESRCH;
    }
    utility_assert(res.event_id == SHD_SHIM_EVENT_SYSCALL_COMPLETE);
    return res.event_data.syscall_complete.retval.as_i64;
}

Thread* threadpreload_new(Host* host, Process* process, gint threadID) {
    ThreadPreload* thread = g_new(ThreadPreload, 1);

    *thread = (ThreadPreload){
        .base = thread_create(host, process, threadID, THREADPRELOAD_TYPE_ID,
                              (ThreadMethods){
                                  .run = threadpreload_run,
                                  .resume = threadpreload_resume,
                                  .handleProcessExit = threadpreload_handleProcessExit,
                                  .getReturnCode = threadpreload_getReturnCode,
                                  .isRunning = threadpreload_isRunning,
                                  .free = threadpreload_free,
                                  .nativeSyscall = threadpreload_nativeSyscall,
                                  .clone = _threadpreload_clone,
                                  .getIPCBlock = _threadpreload_getIPCBlock,
                              }),
    };
    thread->base.sys = syscallhandler_new(host, process, _threadPreloadToThread(thread));

    _Static_assert(sizeof(void*) == 8, "thread-preload impl assumes 8 byte pointers");

    // thread has access to a global, thread safe shared memory manager

    // this function is called when the process is created at the beginning
    // of the sim. but the process may not launch/start until later. any
    // resources for launch/start should be allocated in the respective funcs.

    worker_count_allocation(ThreadPreload);
    return _threadPreloadToThread(thread);
}
