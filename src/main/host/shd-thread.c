/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/shd-thread.h"

#include "main/host/shd-syscall-handler.h"
#include "support/logger/logger.h"

struct _Thread {
    // needs to store comm channel state, etc.

    SysCallHandler* sys;

    gint threadID;
    gint isAlive;

    /* holds the event id for the most recent call from the plugin/shim */
    gint currentEventID;

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

    /* append to the env */
    // TODO fix this to use the correct key/value strings
    myenvv = g_environ_setenv(myenvv, "FTM_EVENT_FD", "0", TRUE);

    // TODO set up the comm channel stuff here

    /* make the fork() / exec() calls */
    // TODO need to fork first, and do any other setup first
    //gint returnValue = execvpe(argv[0], argv, myenvv);

    /* cleanup the dupd env*/
    if(myenvv) {
        g_strfreev(myenvv);
    }

    // TODO get to the point where the plugin blocks before calling main()
    thread->currentEventID = 1; // designate a main() call

    /* thread is now active */
    thread->isAlive = 1;

    /* this will cause us to call main() */
    thread_resume(thread);
}

gint _thread_waitForNextEvent(Thread* thread) {
    MAGIC_ASSERT(thread);

    // TODO wait on the channel for the next event from the shim

    // return the event code
    return 2;
}

void thread_resume(Thread* thread) {
    MAGIC_ASSERT(thread);

    utility_assert(thread->currentEventID != 0);

    SysCallReturn result;

    while(TRUE) {

        switch(thread->currentEventID) {
            case 1: {
                // send the message to the shim to call main(),
                // the plugin will run until it makes a blocking call

                /* event has completed */
                result.block = FALSE;

                break;
            }

            case 2: {
                // deserialize event params from channel
                // some params might be in shared memory
                unsigned int sec = 1;

                // handle call
                result = syscallhandler_sleep(thread->sys, thread, sec);

                if(!result.block) {
                    // TODO send message containing result to shim
                }

                break;
            }

            case 3: {
                unsigned int usec = 1;

                result = syscallhandler_usleep(thread->sys, thread, usec);

                if(!result.block) {
                    // TODO send message containing result to shim
                }

                break;
            }

            case 4: {
                struct timespec req;
                struct timespec rem;

                result = syscallhandler_nanosleep(thread->sys, thread, &req, &rem);

                if(!result.block) {
                    // TODO send message containing result to shim
                }

                break;
            }

            default: {
                error("unknown event type");
                break;
            }
        }

        // TODO remove this once we have functional events
        return;

        if(result.block) {
            /* thread is blocked on simulation progress */
            return;
        } else {
            /* previous event was handled, wait for next one */
            thread->currentEventID = _thread_waitForNextEvent(thread);
        }
    }
}

int thread_terminate(Thread* thread) {
    MAGIC_ASSERT(thread);

    // if the proc has already stopped, just return the returncode

    // terminate the process (send a signal that will cause it to
    // call proper destructors, etc)

    thread->isAlive = 0;

    // return the return code of the process
    return 0;
}

gboolean thread_isAlive(Thread* thread) {
    MAGIC_ASSERT(thread);
    // TODO
    // return TRUE if at least one thread is still running
    // return false if the process died or completed
    return thread->isAlive;
}
