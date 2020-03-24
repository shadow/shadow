/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */

#include "shadow.h"

#include <signal.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shim/shim_event.h"
#include "main/host/shd-thread-protected.h"
#include "main/host/shd-thread-shim.h"
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
};

static ThreadShim* _threadToThreadShim(Thread* thread) {
    utility_assert(thread->type_id == THREADSHIM_TYPE_ID);
    return (ThreadShim*)thread;
}

static Thread* _threadShimToThread(ThreadShim* thread) {
    return (Thread*)thread;
}

void threadshim_free(Thread* base) {
    ThreadShim* thread = _threadToThreadShim(base);

    if (thread->sys) {
        syscallhandler_unref(thread->sys);
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
                    thread->sys, _threadShimToThread(thread),
                    &thread->currentEvent.event_data.syscall.syscall_args);
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
    ThreadShim* thread = _threadToThreadShim(base);
    // TODO [rwails]: come back and make this logic more solid

    MAGIC_ASSERT(base);
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

void* threadshim_clonePluginPtr(Thread* base, PluginPtr plugin_src, size_t n) {
    ThreadShim* thread = _threadToThreadShim(base);

    utility_assert(false);
    return NULL;
    // FIXME(rwails)
    // * Allocate space in shared memory
    // * Send memcpy command via pipe
    // * Return the pointer to shared memory
}

void threadshim_releaseClonedPtr(Thread* base, void* p) {
    ThreadShim* thread = _threadToThreadShim(base);

    utility_assert(false);
    // FIXME(rwails)
    // * Release the pointer
}

const void* threadshim_readPluginPtr(Thread* base, PluginPtr plugin_src,
                                     size_t n) {
    utility_assert(false);
    return NULL;
    // FIXME(rwails)
    // Initial implementation can:
    // * Allocate space in shared memory
    // * Send memcpy command via pipe
    // * Add the pointer to a list to be freed before returning control to the
    // plugin.
    //
    // As an optimization, we could later allow the plugin to request shared
    // regions that *it* owns.  This function could recognize if plugin_src
    // already belongs to such a region, and if so just return the pointer.
}

void* threadshim_writePluginPtr(Thread* base, PluginPtr plugin_src, size_t n) {
    utility_assert(false);
    return NULL;
    // FIXME(rwails)
    // Initial implementation can:
    // * Allocate space in shared memory
    // * Save metadata about this region, s.t. before returning control to the
    // plugin, we tell it to memcpy from the shared region to the original
    // pointer location in its address space.
    //
    // As an optimization, we could later allow the plugin to request shared
    // regions that *it* owns. This function could recognize if plugin_src
    // already belongs to such a region, and if so just return the pointer.
}

Thread* threadshim_new(gint threadID, SysCallHandler* sys) {
    ThreadShim* thread = g_new0(ThreadShim, 1);

    thread->sys = sys;
    syscallhandler_ref(sys);

    thread->threadID = threadID;

    thread->base = (Thread){.run = threadshim_run,
                            .resume = threadshim_resume,
                            .terminate = threadshim_terminate,
                            .setSysCallResult = threadshim_setSysCallResult,
                            .getReturnCode = threadshim_getReturnCode,
                            .isRunning = threadshim_isRunning,
                            .free = threadshim_free,
                            .clonePluginPtr = threadshim_clonePluginPtr,
                            .releaseClonedPtr = threadshim_releaseClonedPtr,

                            .type_id = THREADSHIM_TYPE_ID,
                            .referenceCount = 1};
    MAGIC_INIT(&thread->base);

    // thread has access to a global, thread safe shared memory manager

    // this function is called when the process is created at the beginning
    // of the sim. but the process may not launch/start until later. any
    // resources for launch/start should be allocated in the respective funcs.

    return _threadShimToThread(thread);
}
