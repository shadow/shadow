#include <signal.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <search.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/thread_preload.h"
#include "main/host/thread_protected.h"
#include "main/shmem/shmem_allocator.h"
#include "shim/ipc.h"
#include "shim/shim_event.h"
#include "support/logger/logger.h"

#define THREADPRELOAD_TYPE_ID 13357

struct _ThreadPreload {
    Thread base;

    // needs to store comm channel state, etc.

    SysCallHandler* sys;

    ShMemBlock ipc_blk;

    int isRunning;
    int returnCode;

    /* holds the event id for the most recent call from the plugin/shim */
    ShimEvent currentEvent;

    GHashTable* ptr_to_block;
    GList *read_list, *write_list;
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

static Thread* _threadPreloadToThread(ThreadPreload* thread) {
    return (Thread*)thread;
}

static void _threadpreload_auxFree(void* p, void* _) {
    ShMemBlock* blk = (ShMemBlock*)p;
    shmemallocator_globalFree(blk);
    free(blk);
}

static void _threadpreload_flushReads(ThreadPreload* thread) {
    if (thread->read_list) {
        g_list_foreach(thread->read_list, _threadpreload_auxFree, NULL);
        g_list_free(thread->read_list);
        thread->read_list = NULL;
    }
}

static void _threadpreload_auxWrite(void* p, void* t) {
    ShMemWriteBlock* write_blk = (ShMemWriteBlock*)p;
    ThreadPreload* thread = (ThreadPreload*)t;

    ShimEvent req = {
        .event_id = SHD_SHIM_EVENT_WRITE_REQ,
    };

    ShimEvent resp = {0};

    req.event_data.shmem_blk.serial =
        shmemallocator_globalBlockSerialize(&write_blk->blk);
    req.event_data.shmem_blk.plugin_ptr = write_blk->plugin_ptr;
    req.event_data.shmem_blk.n = write_blk->n;

    shimevent_sendEventToPlugin(thread->ipc_blk.p, &req);
    shimevent_recvEventFromPlugin(thread->ipc_blk.p, &resp);

    utility_assert(resp.event_id == SHD_SHIM_EVENT_SHMEM_COMPLETE);
}

static void _threadpreload_flushWrites(ThreadPreload* thread) {
    if (thread->write_list) {
        g_list_foreach(thread->write_list, _threadpreload_auxWrite, thread);
        g_list_free(thread->write_list);
        thread->write_list = NULL;
    }
}

void threadpreload_free(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    if (thread->sys) {
        syscallhandler_unref(thread->sys);
    }

    if (thread->ptr_to_block) {
        g_hash_table_destroy(thread->ptr_to_block);
    }

    worker_countObject(OBJECT_TYPE_THREAD_PRELOAD, COUNTER_TYPE_FREE);
}

static pid_t _threadpreload_fork_exec(ThreadPreload* thread, const char* file, char* const argv[],
                                      char* const envp[]) {
    pid_t shadow_pid = getpid();
    pid_t pid = vfork();

    switch (pid) {
        case -1:
            error("fork failed");
            return -1;
            break;
        case 0: {
            // child

            // Ensure that the child process exits when Shadow does.  Shadow
            // ought to have already tried to terminate the child via SIGTERM
            // before shutting down (though see
            // https://github.com/shadow/shadow/issues/903), so now we jump all
            // the way to SIGKILL.
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
                error("prctl: %s", g_strerror(errno));
                return -1;
            }
            // Validate that Shadow is still alive (didn't die in between forking and calling
            // prctl).
            if (getppid() != shadow_pid) {
                error("parent (shadow) exited");
                return -1;
            }
            int rc = execvpe(file, argv, envp);
            if (rc == -1) {
                error("execvpe() call failed");
                return -1;
            }
            while (1) {
            } // here for compiler optimization
            break;
        }
        default: // parent
            info("started process %s with PID %d", file, pid);
            return pid;
            break;
    }
}

// status should have been set by caller using waitpid.
static void _threadpreload_cleanup(ThreadPreload* thread, int status) {
    if (WIFEXITED(status)) {
        thread->returnCode = WEXITSTATUS(status);
        debug("child %d exited with status %d", thread->base.nativePid, thread->returnCode);
    } else if (WIFSIGNALED(status)) {
        int signum = WTERMSIG(status);
        debug("child %d terminated by signal %d", thread->base.nativePid, signum);
        thread->returnCode = -1;
    } else {
        debug("child %d quit unexpectedly", thread->base.nativePid);
        thread->returnCode = -1;
    }

    thread->isRunning = 0;
}

pid_t threadpreload_run(Thread* base, gchar** argv, gchar** envv) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    /* set the env for the child */
    gchar** myenvv = g_strdupv(envv);

    thread->ipc_blk = shmemallocator_globalAlloc(ipcData_nbytes());
    utility_assert(thread->ipc_blk.p);
    ipcData_init(thread->ipc_blk.p);

    ShMemBlockSerialized ipc_blk_serial =
        shmemallocator_globalBlockSerialize(&thread->ipc_blk);

    char ipc_blk_buf[SHD_SHMEM_BLOCK_SERIALIZED_MAX_STRLEN] = {0};
    shmemblockserialized_toString(&ipc_blk_serial, ipc_blk_buf);

    /* append to the env */
    myenvv = g_environ_setenv(myenvv, "_SHD_IPC_BLK", ipc_blk_buf, TRUE);

    gchar* envStr = utility_strvToNewStr(myenvv);
    gchar* argStr = utility_strvToNewStr(argv);
    message("forking new thread with environment '%s' and arguments '%s'",
            envStr, argStr);
    g_free(envStr);
    g_free(argStr);

    pid_t child_pid = _threadpreload_fork_exec(thread, argv[0], argv, myenvv);

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
    utility_assert(thread->ipc_blk.p > 0);
    shimevent_recvEventFromPlugin(thread->ipc_blk.p, &thread->currentEvent);
    debug("received shim_event %d", thread->currentEvent.event_id);
}

static void _threadpreload_flushPtrs(ThreadPreload* thread) {
    _threadpreload_flushReads(thread);
    _threadpreload_flushWrites(thread);
}

void threadpreload_flushPtrs(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);
    _threadpreload_flushPtrs(thread);
}

SysCallCondition* threadpreload_resume(Thread* base) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    utility_assert(thread->currentEvent.event_id != SHD_SHIM_EVENT_NULL);

    while (true) {
        switch (thread->currentEvent.event_id) {
            case SHD_SHIM_EVENT_START: {
                // send the message to the shim to call main(),
                // the plugin will run until it makes a blocking call
                debug("sending start event code to %d on %p", thread->base.nativePid,
                      thread->ipc_blk.p);

                thread->currentEvent.event_data.start.simulation_nanos =
                    worker_getEmulatedTime();
                shimevent_sendEventToPlugin(thread->ipc_blk.p, &thread->currentEvent);
                break;
            }
            case SHD_SHIM_EVENT_STOP: {
                // the plugin stopped running, clear it and collect the return
                // code
                int status;
                pid_t rc = waitpid(thread->base.nativePid, &status, 0);
                utility_assert(rc == thread->base.nativePid);
                _threadpreload_cleanup(thread, status);
                // it will not be sending us any more events
                return NULL;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                SysCallReturn result = syscallhandler_make_syscall(
                    thread->sys,
                    &thread->currentEvent.event_data.syscall.syscall_args);

                if (result.state == SYSCALL_BLOCK) {
                    /* thread is blocked on simulation progress */
                    return result.cond;
                }

                _threadpreload_flushPtrs(thread);

                // We've handled the syscall, so we notify that we are done
                // with shmem IPC
                ShimEvent ipc_complete_ev = {
                    .event_id = SHD_SHIM_EVENT_SHMEM_COMPLETE,
                };

                ShimEvent resp = {0};

                shimevent_sendEventToPlugin(thread->ipc_blk.p, &ipc_complete_ev);

                shimevent_recvEventFromPlugin(thread->ipc_blk.p, &resp);

                utility_assert(resp.event_id == SHD_SHIM_EVENT_SHMEM_COMPLETE);

                ShimEvent shim_result;
                if (result.state == SYSCALL_DONE) {
                    // Now send the result of the syscall
                    shim_result = (ShimEvent){
                        .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                        .event_data = {
                            .syscall_complete = {.retval = result.retval,
                                                 .simulation_nanos =
                                                     worker_getEmulatedTime()},

                        }};
                } else if (result.state == SYSCALL_NATIVE) {
                    // Tell the shim to make the syscall itself
                    shim_result = (ShimEvent){
                        .event_id = SHD_SHIM_EVENT_SYSCALL_DO_NATIVE,
                    };
                }
                shimevent_sendEventToPlugin(thread->ipc_blk.p, &shim_result);
                break;
            }
            case SHD_SHIM_EVENT_SYSCALL_COMPLETE: {
                shimevent_sendEventToPlugin(thread->ipc_blk.p, &thread->currentEvent);
                break;
            }
            default: {
                error("unknown event type");
                break;
            }
        }

        /* previous event was handled, wait for next one */
        _threadpreload_waitForNextEvent(thread);
    }
}

void threadpreload_terminate(Thread* base) {
    MAGIC_ASSERT(base);
    ThreadPreload* thread = _threadToThreadPreload(base);
    // TODO [rwails]: come back and make this logic more solid

    /* make sure we cleanup circular refs */
    if (thread->sys) {
        syscallhandler_unref(thread->sys);
        thread->sys = NULL;
    }

    if (!thread->isRunning) {
        return;
    }

    int status = 0;

    utility_assert(thread->base.nativePid > 0);

    pid_t rc = waitpid(thread->base.nativePid, &status, WNOHANG);
    utility_assert(rc != -1);

    if (rc == 0) { // child is running, request a stop
        debug("sending SIGTERM to %d", thread->base.nativePid);
        kill(thread->base.nativePid, SIGTERM);
        rc = waitpid(thread->base.nativePid, &status, 0);
        utility_assert(rc != -1 && rc > 0);
    }
    _threadpreload_cleanup(thread, status);
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

/*
 * Helper function, issues a clone/read request to the plugin.
 * The returned ShMemBlock is owned by the caller and needs to be freed.
 */
static ShMemBlock* _threadpreload_readPtrImpl(ThreadPreload* thread,
                                              PluginPtr plugin_src, size_t n,
                                              bool is_string) {

    // Allocate a block for the clone
    ShMemBlock* blk = calloc(1, sizeof(ShMemBlock));
    *blk = shmemallocator_globalAlloc(n);

    utility_assert(blk->p && blk->nbytes == n);

    ShimEvent req = {
        .event_id = SHD_SHIM_EVENT_CLONE_REQ,
    };

    ShimEvent resp = {0};

    req.event_id = is_string ? SHD_SHIM_EVENT_CLONE_STRING_REQ : SHD_SHIM_EVENT_CLONE_REQ;
    req.event_data.shmem_blk.serial = shmemallocator_globalBlockSerialize(blk);
    req.event_data.shmem_blk.plugin_ptr = plugin_src;
    req.event_data.shmem_blk.n = n;

    shimevent_sendEventToPlugin(thread->ipc_blk.p, &req);
    shimevent_recvEventFromPlugin(thread->ipc_blk.p, &resp);

    utility_assert(resp.event_id == SHD_SHIM_EVENT_SHMEM_COMPLETE);

    return blk;
}

void* threadpreload_newClonedPtr(Thread* base, PluginPtr plugin_src, size_t n) {
    ThreadPreload* thread = _threadToThreadPreload(base);
    ShMemBlock* blk = _threadpreload_readPtrImpl(thread, plugin_src, n, false);
    g_hash_table_insert(thread->ptr_to_block, &blk->p, blk);
    return blk->p;
}

void threadpreload_releaseClonedPtr(Thread* base, void* p) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    ShMemBlock* blk = g_hash_table_lookup(thread->ptr_to_block, &p);
    utility_assert(blk != NULL);
    g_hash_table_remove(thread->ptr_to_block, &p);
    shmemallocator_globalFree(blk);
    free(blk);
}

const void* threadpreload_getReadablePtr(Thread* base, PluginPtr plugin_src,
                                         size_t n) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    ShMemBlock* blk = _threadpreload_readPtrImpl(thread, plugin_src, n, false);

    GList* new_head = g_list_append(thread->read_list, blk);
    utility_assert(new_head);
    if (!thread->read_list) {
        thread->read_list = new_head;
    }

    return blk->p;
}

int threadpreload_getReadableString(Thread* base, PluginPtr plugin_src, size_t n,
                             const char** str_out, size_t* strlen_out) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    ShMemBlock* blk = _threadpreload_readPtrImpl(thread, plugin_src, n, true);

    const char* str = blk->p;
    size_t strlen = strnlen(str, n);
    if (strlen == n) {
        shmemallocator_globalFree(blk);
        return -ENAMETOOLONG;
    }
    if (strlen_out) {
        *strlen_out = strlen;
    }

    GList* new_head = g_list_append(thread->read_list, blk);
    utility_assert(new_head);
    if (!thread->read_list) {
        thread->read_list = new_head;
    }

    utility_assert(str_out);
    *str_out = blk->p;
    return 0;
}

void* threadpreload_getWriteablePtr(Thread* base, PluginPtr plugin_src,
                                    size_t n) {
    ThreadPreload* thread = _threadToThreadPreload(base);

    // Allocate a block for the clone
    ShMemWriteBlock* write_blk = calloc(1, sizeof(ShMemWriteBlock));
    utility_assert(write_blk);
    write_blk->blk = shmemallocator_globalAlloc(n);
    write_blk->plugin_ptr = plugin_src;
    write_blk->n = n;

    utility_assert(write_blk->blk.p && write_blk->blk.nbytes == n);

    GList* new_head = g_list_append(thread->write_list, write_blk);
    utility_assert(new_head);
    if (!thread->write_list) {
        thread->write_list = new_head;
    }

    return write_blk->blk.p;
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
    for (int i=0; i<6; ++i) {
        req.event_data.syscall.syscall_args.args[i].as_i64 = va_arg(args, int64_t);
    }
    shimevent_sendEventToPlugin(thread->ipc_blk.p, &req);

    ShimEvent resp = {0};
    shimevent_recvEventFromPlugin(thread->ipc_blk.p, &resp);
    utility_assert(resp.event_id == SHD_SHIM_EVENT_SYSCALL_COMPLETE);
    return resp.event_data.syscall_complete.retval.as_i64;
}

Thread* threadpreload_new(Host* host, Process* process, gint threadID) {
    ThreadPreload* thread = g_new(ThreadPreload, 1);

    *thread = (ThreadPreload){
        .base = thread_create(host, process, THREADPRELOAD_TYPE_ID,
                              (ThreadMethods){
                                  .run = threadpreload_run,
                                  .resume = threadpreload_resume,
                                  .terminate = threadpreload_terminate,
                                  .getReturnCode = threadpreload_getReturnCode,
                                  .isRunning = threadpreload_isRunning,
                                  .free = threadpreload_free,
                                  .newClonedPtr = threadpreload_newClonedPtr,
                                  .releaseClonedPtr = threadpreload_releaseClonedPtr,
                                  .getReadablePtr = threadpreload_getReadablePtr,
                                  .getReadableString = threadpreload_getReadableString,
                                  .getWriteablePtr = threadpreload_getWriteablePtr,
                                  .flushPtrs = threadpreload_flushPtrs,
                                  .nativeSyscall = threadpreload_nativeSyscall,
                              }),
        .ptr_to_block = g_hash_table_new(g_int64_hash, g_int64_equal),
        .read_list = NULL,
        .write_list = NULL,
    };
    thread->sys = syscallhandler_new(host, process, _threadPreloadToThread(thread));

    _Static_assert(
        sizeof(void*) == 8, "thread-preload impl assumes 8 byte pointers");

    // thread has access to a global, thread safe shared memory manager

    // this function is called when the process is created at the beginning
    // of the sim. but the process may not launch/start until later. any
    // resources for launch/start should be allocated in the respective funcs.

    worker_countObject(OBJECT_TYPE_THREAD_PRELOAD, COUNTER_TYPE_NEW);
    return _threadPreloadToThread(thread);
}
