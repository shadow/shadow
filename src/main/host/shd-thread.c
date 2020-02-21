/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/shd-thread.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shim/shim_event.h"
#include "main/host/shd-syscall-handler.h"
#include "support/logger/logger.h"


struct _Thread {
    // needs to store comm channel state, etc.

    SysCallHandler* sys;

    pid_t childPID;
    int eventFD;

    gint threadID;
    gint isAlive;

    /* holds the event id for the most recent call from the plugin/shim */
    ShimEvent currentEvent;

    gint referenceCount;
    MAGIC_DECLARE;
};

Thread* thread_new(gint threadID, SysCallHandler* sys) {
    Thread* thread = g_new0(Thread, 1);
    MAGIC_INIT(thread);

    thread->sys = sys;
    syscallhandler_ref(sys);

    thread->threadID = threadID;

    thread->referenceCount = 1;

    // thread has access to a global, thread safe shared memory manager

    // this function is called when the process is created at the beginning
    // of the sim. but the process may not launch/start until later. any
    // resources for launch/start should be allocated in the respective funcs.

    return thread;
}

static void _thread_free(Thread* thread) {
    MAGIC_ASSERT(thread);

    if(thread->sys) {
        syscallhandler_unref(thread->sys);
    }

    MAGIC_CLEAR(thread);
    g_free(thread);
}

static void _thread_create_ipc_sockets(Thread *thread, int *child_fd) {
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

static int _thread_fork_exec(Thread *thread,
                             const char *file,
                             char *const argv[],
                             char *const envp[])
{
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
            while (1) {} // here for compiler optimization
            break;
        default: // parent
            info("started process %s with PID %d", file, pid);
            thread->childPID = pid;
            return pid;
    }
}

void thread_ref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)++;
}

void thread_unref(Thread* thread) {
    MAGIC_ASSERT(thread);
    (thread->referenceCount)--;
    utility_assert(thread->referenceCount >= 0);
    if(thread->referenceCount == 0) {
        _thread_free(thread);
    }
}

void thread_run(Thread* thread, gchar** argv, gchar** envv) {
    MAGIC_ASSERT(thread);

    /* set the env for the child */
    gchar** myenvv = g_strdupv(envv);

    int child_fd = 0;
    _thread_create_ipc_sockets(thread, &child_fd);
    utility_assert(thread->eventFD != -1 && child_fd != -1);

    char buf[64];
    snprintf(buf, 64, "%d", child_fd);

    /* append to the env */
    myenvv = g_environ_setenv(myenvv, "_SHD_IPC_SOCKET", buf, TRUE);

    _thread_fork_exec(thread, argv[0], argv, myenvv);

    // close the child sock, it is no longer needed
    close(child_fd);

    /* cleanup the dupd env*/
    if(myenvv) {
        g_strfreev(myenvv);
    }

    // TODO get to the point where the plugin blocks before calling main()
    thread->currentEvent.event_id = SHD_SHIM_EVENT_START;

    /* thread is now active */
    thread->isAlive = 1;

    /* this will cause us to call main() */
    thread_resume(thread);
}

static inline void _thread_waitForNextEvent(Thread* thread) {
    MAGIC_ASSERT(thread);
    utility_assert(thread->eventFD > 0);
    shimevent_recvEvent(thread->eventFD, &thread->currentEvent);
    debug("received shim_event %d", thread->currentEvent.event_id);
}

void thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);

    utility_assert(thread->currentEvent.event_id != SHD_SHIM_EVENT_NULL);

    SysCallReturn result;

    while(TRUE) {

        switch(thread->currentEvent.event_id) {
            case SHD_SHIM_EVENT_START: {
                // send the message to the shim to call main(),
                // the plugin will run until it makes a blocking call
                debug("sending start event code to %d on %d", thread->childPID, thread->eventFD);
                shimevent_sendEvent(thread->eventFD, &thread->currentEvent);

                /* event has completed */
                result.block = FALSE;

                break;
            }

            case SHD_SHIM_EVENT_NANO_SLEEP: {
                struct timespec req =
                    thread->currentEvent.event_data.data_nano_sleep.ts;

                struct timespec rem;

                result = syscallhandler_nanosleep(thread->sys, thread, &req, &rem);

                if(!result.block) {
                    // TODO send message containing result to shim
                } else {
                    // TODO (rwails): stub, change me
                    ShimEvent next_ev;
                    next_ev.event_id = SHD_SHIM_EVENT_NANO_SLEEP_COMPLETE;
                    next_ev.event_data.rv = result.retval._int;

                    thread->currentEvent = next_ev;
                }

                break;
            }

            case SHD_SHIM_EVENT_NANO_SLEEP_COMPLETE: {

                ShimEvent response;
                response.event_id = SHD_SHIM_EVENT_NANO_SLEEP;
                response.event_data.data_nano_sleep.ts.tv_sec = 0;
                response.event_data.data_nano_sleep.ts.tv_nsec = 0;
                shimevent_sendEvent(thread->eventFD, &response);

                break;
            }

            default: {
                error("unknown event type");
                break;
            }
        }

        if(result.block) {
            /* thread is blocked on simulation progress */
            return;
        } else {
            /* previous event was handled, wait for next one */
            _thread_waitForNextEvent(thread);
        }
    }
}

int thread_terminate(Thread* thread) {
    // TODO [rwails]: come back and make this logic more solid

    MAGIC_ASSERT(thread);

    int status = 0, child_rc = 0;

    if (thread->isAlive) {

        utility_assert(thread->childPID > 0);

        pid_t rc = waitpid(thread->childPID, &status, WNOHANG);
        utility_assert(rc != -1);

        if (rc == 0) { // child is running, request a stop
            debug("sending SIGTERM to %d", thread->childPID);
            kill(thread->childPID, SIGTERM);
            rc = waitpid(thread->childPID, &status, 0);
            utility_assert(rc != -1 && rc > 0);
        }

        if (WIFEXITED(status)) {
            child_rc = WEXITSTATUS(status);
            debug("child %d exited with status %d", thread->childPID, child_rc);
        } else if (WIFSIGNALED(status)) {
            int signum = WTERMSIG(status);
            debug("child %d terminated by signal %d", thread->childPID, signum);
            child_rc = -1;
        } else {
            debug("child %d quit unexpectedly", thread->childPID);
        }

        thread->isAlive = 0;
    }

    // return the return code of the process
    return child_rc;
}

gboolean thread_isAlive(Thread* thread) {
    MAGIC_ASSERT(thread);
    // TODO
    // return TRUE if at least one thread is still running
    // return false if the process died or completed
    return thread->isAlive;
}
