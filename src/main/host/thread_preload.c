#include "main/host/thread_preload.h"

#include <signal.h>
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

void threadpreload_free(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    if (thread->base.sys) {
        syscallhandler_unref(thread->base.sys);
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

static gchar** _add_shadow_pid_to_env(gchar** envp) {

    enum { BUF_NBYTES = 256 };
    char strbuf[BUF_NBYTES] = {0};

    pid_t shadow_pid = getpid();

    snprintf(strbuf, BUF_NBYTES, "%llu", (unsigned long long)shadow_pid);

    envp = g_environ_setenv(envp, "SHADOW_PID", strbuf, TRUE);

    return envp;
}

static pid_t _threadpreload_fork_exec(ThreadPreload* thread, const char* file, char* const argv[],
                                      char* const envp[], const char* workingDir) {
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
            debug("started process %s with PID %d", file, pid);
            return pid;
            break;
    }
}

static void _threadpreload_cleanup(ThreadPreload* thread) {
    trace("child %d exited", thread->base.nativePid);
    thread->isRunning = 0;

    if (thread->base.sys) {
        syscallhandler_unref(thread->base.sys);
        thread->base.sys = NULL;
    }
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
    myenvv = _add_shadow_pid_to_env(myenvv);

    gchar* envStr = utility_strvToNewStr(myenvv);
    gchar* argStr = utility_strvToNewStr(argv);
    info("forking new thread with environment '%s', arguments '%s', and working directory '%s'",
         envStr, argStr, workingDir);
    g_free(envStr);
    g_free(argStr);

    pid_t child_pid = _threadpreload_fork_exec(thread, argv[0], argv, myenvv, workingDir);
    ipcData_registerPluginPid(thread->ipc_data, child_pid);

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

static inline void _threadpreload_waitForNextEvent(ThreadPreload* thread) {
    MAGIC_ASSERT(_threadPreloadToThread(thread));
    utility_assert(thread->ipc_data);
    shimevent_recvEventFromPlugin(thread->ipc_data, &thread->currentEvent);
    trace("received shim_event %d", thread->currentEvent.event_id);
}

static ShMemBlock* _threadpreload_getIPCBlock(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);
    return &thread->ipc_blk;
}

static ShMemBlock* _threadpreload_getShMBlock(Thread* base) {
    // We currently communicate the simulation time to the shim by including it in every event
    // we send over the IPC channel, and the shim caches it.
    // TODO we could instead use a shmem segment like threadptrace does.
    return NULL;
}

SysCallCondition* threadpreload_resume(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);

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

                thread->currentEvent.event_data.start.simulation_nanos = worker_getEmulatedTime();
                shimevent_sendEventToPlugin(thread->ipc_data, &thread->currentEvent);
                break;
            }
            case SHD_SHIM_EVENT_STOP: {
                // the plugin stopped running
                _threadpreload_cleanup(thread);
                // it will not be sending us any more events
                return NULL;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                // XXX hacky. Move to a syscall handler?
                if (thread->currentEvent.event_data.syscall.syscall_args.number == SYS_exit) {
                    // Tell thread to go ahead and make the exit syscall itself.
                    shimevent_sendEventToPlugin(thread->ipc_data, &(ShimEvent){
                        .event_id = SHD_SHIM_EVENT_SYSCALL_DO_NATIVE
                    });
                    // Clean up the thread.
                    _threadpreload_cleanup(thread);
                    return NULL;
                }

                SysCallReturn result = syscallhandler_make_syscall(
                    thread->base.sys, &thread->currentEvent.event_data.syscall.syscall_args);

                // Flush any writes the syscallhandler made.
                process_flushPtrs(thread->base.process);

                if (result.state == SYSCALL_BLOCK) {
                    if (shimipc_sendExplicitBlockMessageEnabled()) {
                        trace("Sending block message to plugin");
                        // thread is blocked on simulation progress. Tell it to
                        // stop spinning so that releases its CPU core for the next
                        // thread to be run.
                        ShimEvent block_event = {.event_id = SHD_SHIM_EVENT_BLOCK};
                        shimevent_sendEventToPlugin(thread->ipc_data, &block_event);
                        shimevent_recvEventFromPlugin(thread->ipc_data, &block_event);
                    }

                    return result.cond;
                }

                ShimEvent shim_result;
                if (result.state == SYSCALL_DONE) {
                    // Now send the result of the syscall
                    shim_result = (ShimEvent){
                        .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                        .event_data = {
                            .syscall_complete = {.retval = result.retval,
                                                 .simulation_nanos = worker_getEmulatedTime(),
                                                 },

                        }};
                } else if (result.state == SYSCALL_NATIVE) {
                    // Tell the shim to make the syscall itself
                    shim_result = (ShimEvent){
                        .event_id = SHD_SHIM_EVENT_SYSCALL_DO_NATIVE,
                    };
                }
                shimevent_sendEventToPlugin(thread->ipc_data, &shim_result);
                break;
            }
            case SHD_SHIM_EVENT_SYSCALL_COMPLETE: {
                shimevent_sendEventToPlugin(thread->ipc_data, &thread->currentEvent);
                break;
            }
            default: {
                utility_panic("unknown event type");
                break;
            }
        }

        /* previous event was handled, wait for next one */
        _threadpreload_waitForNextEvent(thread);
    }
}

void threadpreload_handleProcessExit(Thread* base) {
    MAGIC_ASSERT(base);
    ThreadPreload* thread = _threadToThreadPreload(base);
    // TODO [rwails]: come back and make this logic more solid

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
    ipcData_registerPluginPid(child->ipc_data, base->nativePid);
    ShMemBlockSerialized ipc_blk_serial = shmemallocator_globalBlockSerialize(&child->ipc_blk);

    // Send an IPC block for the new thread to use.
    shimevent_sendEventToPlugin(thread->ipc_data, &(ShimEvent){
        .event_id = SHD_SHIM_EVENT_ADD_THREAD_REQ,
        .event_data.add_thread_req = {
            .ipc_block = ipc_blk_serial,
        }
    });
    {
        ShimEvent res;
        shimevent_recvEventFromPlugin(thread->ipc_data, &res);
        utility_assert(res.event_id == SHD_SHIM_EVENT_ADD_THREAD_PARENT_RES);
    }

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
    shimevent_sendEventToPlugin(thread->ipc_data, &req);

    ShimEvent resp = {0};
    shimevent_recvEventFromPlugin(thread->ipc_data, &resp);
    utility_assert(resp.event_id == SHD_SHIM_EVENT_SYSCALL_COMPLETE);
    return resp.event_data.syscall_complete.retval.as_i64;
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
                                  .getShMBlock = _threadpreload_getShMBlock,
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
