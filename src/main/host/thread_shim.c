#include "main/host/thread_shim.h"

/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */

#include <signal.h>
#include <string.h>

#include <fcntl.h>
#include <glib.h>
#include <search.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shim/shim_event.h"
#include "main/host/thread_protected.h"
#include "main/host/thread_shim.h"
#include "main/shmem/shmem_allocator.h"
#include "support/logger/logger.h"

#define THREADSHIM_TYPE_ID 13357

struct _ThreadShim {
    Thread base;

    // needs to store comm channel state, etc.

    SysCallHandler* sys;

    pid_t childPID;
    int eventFD;

    int threadID;
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

static ThreadShim* _threadToThreadShim(Thread* thread) {
    utility_assert(thread->type_id == THREADSHIM_TYPE_ID);
    return (ThreadShim*)thread;
}

static Thread* _threadShimToThread(ThreadShim* thread) {
    return (Thread*)thread;
}

static void _threadshim_auxFree(void* p, void* _) {
    ShMemBlock* blk = (ShMemBlock*)p;
    shmemallocator_globalFree(blk);
    free(blk);
}

static void _threadshim_flushReads(ThreadShim* thread) {
    if (thread->read_list) {
        g_list_foreach(thread->read_list, _threadshim_auxFree, NULL);
        g_list_free(thread->read_list);
        thread->read_list = NULL;
    }
}

static void _threadshim_auxWrite(void* p, void* t) {
    ShMemWriteBlock* write_blk = (ShMemWriteBlock*)p;
    ThreadShim* thread = (ThreadShim*)t;

    ShimEvent req = {
        .event_id = SHD_SHIM_EVENT_WRITE_REQ,
    };

    ShimEvent resp = {0};

    req.event_data.shmem_blk.serial =
        shmemallocator_globalBlockSerialize(&write_blk->blk);
    req.event_data.shmem_blk.plugin_ptr = write_blk->plugin_ptr;
    req.event_data.shmem_blk.n = write_blk->n;

    shimevent_sendEvent(thread->eventFD, &req);
    shimevent_recvEvent(thread->eventFD, &resp);

    utility_assert(resp.event_id == SHD_SHIM_EVENT_SHMEM_COMPLETE);
}

static void _threadshim_flushWrites(ThreadShim* thread) {
    if (thread->write_list) {
        g_list_foreach(thread->write_list, _threadshim_auxWrite, thread);
        g_list_free(thread->write_list);
        thread->write_list = NULL;
    }
}

void threadshim_free(Thread* base) {
    ThreadShim* thread = _threadToThreadShim(base);

    if (thread->sys) {
        syscallhandler_unref(thread->sys);
    }

    if (thread->ptr_to_block) {
        g_hash_table_destroy(thread->ptr_to_block);
    }

    MAGIC_CLEAR(base);
    g_free(thread);
}

static void _threadshim_create_ipc_sockets(ThreadShim* thread, int* child_fd) {
    utility_assert(thread != NULL && child_fd != NULL);

    int socks[2] = {0, 0};

    int rc = socketpair(AF_UNIX, SOCK_DGRAM, 0, socks);

    if (rc == 0) {
        thread->eventFD = socks[0];
        *child_fd = socks[1];

        // set the parent fd to close on exec
        fcntl(thread->eventFD, F_SETFD, FD_CLOEXEC);
    } else {
        error("socketpair() call failed");
        thread->eventFD = *child_fd = -1;
    }
}

static int _threadshim_fork_exec(ThreadShim* thread, const char* file,
                                 char* const argv[], char* const envp[]) {
    int rc = 0;
    pid_t pid = fork();

    switch (pid) {
        case -1:
            error("fork failed");
            return -1;
            break;
        case 0: // child
            execvpe(file, argv, envp);
            if (rc == -1) {
                error("execvpe() call failed");
                return -1;
            }
            while (1) {
            } // here for compiler optimization
            break;
        default: // parent
            info("started process %s with PID %d", file, pid);
            thread->childPID = pid;
            return pid;
            break;
    }
}

// status should have been set by caller using waitpid.
static void _threadshim_cleanup(ThreadShim* thread, int status) {
    if (WIFEXITED(status)) {
        thread->returnCode = WEXITSTATUS(status);
        debug("child %d exited with status %d", thread->childPID,
              thread->returnCode);
    } else if (WIFSIGNALED(status)) {
        int signum = WTERMSIG(status);
        debug("child %d terminated by signal %d", thread->childPID, signum);
        thread->returnCode = -1;
    } else {
        debug("child %d quit unexpectedly", thread->childPID);
        thread->returnCode = -1;
    }

    thread->isRunning = 0;
}

void threadshim_run(Thread* base, gchar** argv, gchar** envv) {
    ThreadShim* thread = _threadToThreadShim(base);

    /* set the env for the child */
    gchar** myenvv = g_strdupv(envv);

    int child_fd = 0;
    _threadshim_create_ipc_sockets(thread, &child_fd);
    utility_assert(thread->eventFD != -1 && child_fd != -1);

    char buf[64];
    snprintf(buf, 64, "%d", child_fd);

    /* append to the env */
    myenvv = g_environ_setenv(myenvv, "_SHD_IPC_SOCKET", buf, TRUE);

    gchar* envStr = utility_strvToNewStr(myenvv);
    gchar* argStr = utility_strvToNewStr(argv);
    message("forking new thread with environment '%s' and arguments '%s'",
            envStr, argStr);
    g_free(envStr);
    g_free(argStr);

    _threadshim_fork_exec(thread, argv[0], argv, myenvv);

    // close the child sock, it is no longer needed
    close(child_fd);

    /* cleanup the dupd env*/
    if (myenvv) {
        g_strfreev(myenvv);
    }

    // TODO get to the point where the plugin blocks before calling main()
    thread->currentEvent.event_id = SHD_SHIM_EVENT_START;

    /* thread is now active */
    thread->isRunning = 1;

    /* this will cause us to call main() */
    thread_resume(_threadShimToThread(thread));
}

static inline void _threadshim_waitForNextEvent(ThreadShim* thread) {
    MAGIC_ASSERT(_threadShimToThread(thread));
    utility_assert(thread->eventFD > 0);
    shimevent_recvEvent(thread->eventFD, &thread->currentEvent);
    debug("received shim_event %d", thread->currentEvent.event_id);
}

void threadshim_resume(Thread* base) {
    ThreadShim* thread = _threadToThreadShim(base);

    utility_assert(thread->currentEvent.event_id != SHD_SHIM_EVENT_NULL);

    bool blocked = false;

    while (!blocked) {
        switch (thread->currentEvent.event_id) {
            case SHD_SHIM_EVENT_START: {
                // send the message to the shim to call main(),
                // the plugin will run until it makes a blocking call
                debug("sending start event code to %d on %d", thread->childPID,
                      thread->eventFD);
                shimevent_sendEvent(thread->eventFD, &thread->currentEvent);
                break;
            }
            case SHD_SHIM_EVENT_STOP: {
                // the plugin stopped running, clear it and collect the return
                // code
                int status;
                pid_t rc = waitpid(thread->childPID, &status, 0);
                utility_assert(rc == thread->childPID);
                _threadshim_cleanup(thread, status);
                // it will not be sending us any more events
                return;
            }
            case SHD_SHIM_EVENT_SYSCALL: {
                SysCallReturn result = syscallhandler_make_syscall(
                    thread->sys,
                    &thread->currentEvent.event_data.syscall.syscall_args);

                // FIXME(rwails) I need to update where this occurs when we
                // implement blocking calls

                _threadshim_flushReads(thread);
                _threadshim_flushWrites(thread);

                // We've handled the syscall, so we notify that we are done
                // with shmem IPC
                ShimEvent ipc_complete_ev = {
                    .event_id = SHD_SHIM_EVENT_SHMEM_COMPLETE,
                };

                shimevent_sendEvent(thread->eventFD, &ipc_complete_ev);

                if (result.state == SYSCALL_RETURN_DONE) {
                    ShimEvent shim_result = {
                        .event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                        .event_data.syscall_complete.retval = result.retval};
                    shimevent_sendEvent(thread->eventFD, &shim_result);
                } else {
                    // FIXME: SYSCALL_RETURN_NATIVE unhandled, and we might want
                    // it e.g. for a read that turns out to be to a file rather
                    // than a socket.
                    utility_assert(result.state == SYSCALL_RETURN_BLOCKED);
                    blocked = true;
                }
                break;
            }
            case SHD_SHIM_EVENT_SYSCALL_COMPLETE: {
                shimevent_sendEvent(thread->eventFD, &thread->currentEvent);
                break;
            }
            default: {
                error("unknown event type");
                break;
            }
        }

        if (blocked) {
            /* thread is blocked on simulation progress */
            return;
        } else {
            /* previous event was handled, wait for next one */
            _threadshim_waitForNextEvent(thread);
        }
    }
}

void threadshim_setSysCallResult(Thread* base, SysCallReg retval) {
    ThreadShim* thread = _threadToThreadShim(base);
    thread->currentEvent =
        (ShimEvent){.event_id = SHD_SHIM_EVENT_SYSCALL_COMPLETE,
                    .event_data.syscall_complete.retval = retval};
}

void threadshim_terminate(Thread* base) {
    MAGIC_ASSERT(base);
    ThreadShim* thread = _threadToThreadShim(base);
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

    utility_assert(thread->childPID > 0);

    pid_t rc = waitpid(thread->childPID, &status, WNOHANG);
    utility_assert(rc != -1);

    if (rc == 0) { // child is running, request a stop
        debug("sending SIGTERM to %d", thread->childPID);
        kill(thread->childPID, SIGTERM);
        rc = waitpid(thread->childPID, &status, 0);
        utility_assert(rc != -1 && rc > 0);
    }
    _threadshim_cleanup(thread, status);
}

int threadshim_getReturnCode(Thread* base) {
    ThreadShim* thread = _threadToThreadShim(base);
    return thread->returnCode;
}

gboolean threadshim_isRunning(Thread* base) {
    ThreadShim* thread = _threadToThreadShim(base);

    // TODO
    // return TRUE if at least one thread is still running
    // return false if the process died or completed
    return thread->isRunning;
}

/*
 * Helper function, issues a clone/read request to the plugin.
 * The returned ShMemBlock is owned by the caller and needs to be freed.
 */
static ShMemBlock* _threadshim_readPtrImpl(ThreadShim* thread,
                                           PluginPtr plugin_src, size_t n) {

    // Allocate a block for the clone
    ShMemBlock* blk = calloc(1, sizeof(ShMemBlock));
    *blk = shmemallocator_globalAlloc(n);

    utility_assert(blk->p && blk->nbytes == n);

    ShimEvent req = {
        .event_id = SHD_SHIM_EVENT_CLONE_REQ,
    };

    ShimEvent resp = {0};

    req.event_id = SHD_SHIM_EVENT_CLONE_REQ;
    req.event_data.shmem_blk.serial = shmemallocator_globalBlockSerialize(blk);
    req.event_data.shmem_blk.plugin_ptr = plugin_src;
    req.event_data.shmem_blk.n = n;

    shimevent_sendEvent(thread->eventFD, &req);
    shimevent_recvEvent(thread->eventFD, &resp);

    utility_assert(resp.event_id == SHD_SHIM_EVENT_SHMEM_COMPLETE);

    return blk;
}

void* threadshim_clonePluginPtr(Thread* base, PluginPtr plugin_src, size_t n) {
    ThreadShim* thread = _threadToThreadShim(base);
    ShMemBlock* blk = _threadshim_readPtrImpl(thread, plugin_src, n);
    g_hash_table_insert(thread->ptr_to_block, &blk->p, blk);
    return blk->p;
}

void threadshim_releaseClonedPtr(Thread* base, void* p) {
    ThreadShim* thread = _threadToThreadShim(base);

    ShMemBlock* blk = g_hash_table_lookup(thread->ptr_to_block, &p);
    utility_assert(blk != NULL);
    g_hash_table_remove(thread->ptr_to_block, &p);
    shmemallocator_globalFree(blk);
    free(blk);
}

const void* threadshim_readPluginPtr(Thread* base, PluginPtr plugin_src,
                                     size_t n) {
    ThreadShim* thread = _threadToThreadShim(base);

    ShMemBlock* blk = _threadshim_readPtrImpl(thread, plugin_src, n);

    GList* new_head = g_list_append(thread->read_list, blk);
    utility_assert(new_head);
    if (!thread->read_list) {
        thread->read_list = new_head;
    }

    return blk->p;
}

void* threadshim_writePluginPtr(Thread* base, PluginPtr plugin_src, size_t n) {
    ThreadShim* thread = _threadToThreadShim(base);

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

Thread* threadshim_new(Host* host, Process* process, gint threadID) {
    ThreadShim* thread = g_new0(ThreadShim, 1);

    thread->base = (Thread){.run = threadshim_run,
                            .resume = threadshim_resume,
                            .terminate = threadshim_terminate,
                            .setSysCallResult = threadshim_setSysCallResult,
                            .getReturnCode = threadshim_getReturnCode,
                            .isRunning = threadshim_isRunning,
                            .free = threadshim_free,
                            .clonePluginPtr = threadshim_clonePluginPtr,
                            .releaseClonedPtr = threadshim_releaseClonedPtr,
                            .readPluginPtr = threadshim_readPluginPtr,
                            .writePluginPtr = threadshim_writePluginPtr,
                            .type_id = THREADSHIM_TYPE_ID,
                            .referenceCount = 1};
    MAGIC_INIT(&thread->base);

    thread->threadID = threadID;
    thread->sys =
        syscallhandler_new(host, process, _threadShimToThread(thread));

    _Static_assert(
        sizeof(void*) == 8, "thread-shim impl assumes 8 byte pointers");
    thread->ptr_to_block = g_hash_table_new(g_int64_hash, g_int64_equal);
    thread->read_list = NULL;
    thread->write_list = NULL;

    // thread has access to a global, thread safe shared memory manager

    // this function is called when the process is created at the beginning
    // of the sim. but the process may not launch/start until later. any
    // resources for launch/start should be allocated in the respective funcs.

    return _threadShimToThread(thread);
}
