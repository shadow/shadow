/*
 * shd-thread.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/shd-thread.h"

#include "main/host/shd-syscall-handler.h"

struct _Thread {
    // needs to store comm channel state, etc.

    SystemCallHandler* sys;

    int isAlive;

    gint referenceCount;
    MAGIC_DECLARE;
};

Thread* thread_new(SystemCallHandler* sys) {
    Thread* thread = g_new0(Thread, 1);
    MAGIC_INIT(thread);

    thread->sys = sys;
    syscallhandler_ref(sys);

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

static void _thread_launch(Thread* thread) {
    MAGIC_ASSERT(thread);
    thread->isAlive = 1;
    // launch the plugin process, set up the comm channel, get the
    // process to the point where it blocks before calling main()
}

void thread_start(Thread* thread, int argc, char** argv) {
    MAGIC_ASSERT(thread);

    _thread_launch(thread);

    // call main()
    // the plugin will run until it makes a blocking call
    // return
}

void thread_continue(Thread* thread) {
    MAGIC_ASSERT(thread);

    // TODO somehow we need to figure out which thread_key we are running
    int threadKey = 0;

    // "main" loop
    while(TRUE) {
        // unblock one of the threads in the plugin (ie a channel)
        // when we get here, we should know which thread_key should be unblocked
        // because the syscallhandler will track it when blocking

        // wait for event in channel
        int event = 0;

        // handle event
        switch(event) {
            case 1: {
                // deserialize event params from channel
                // some params might be in shared memory
                unsigned int sec = 1;

                // handle call
                unsigned int result = syscallhandler_sleep(thread->sys, threadKey, sec);

                // if return variable indicates that the call is blocked
                // we need to block the plugin, i.e. mark thread_key as blocked
                // and then move to the next thread
                // TODO how to mark threadKey as blocked?

                break;
            }

            case 2: {
                unsigned int usec = 1;
                int result = syscallhandler_usleep(thread->sys, threadKey, usec);
                break;
            }

            case 3: {
                struct timespec req;
                struct timespec rem;
                int result = syscallhandler_nanosleep(thread->sys, threadKey, &req, &rem);
                break;
            }

            default: {
                break;
            }
        }


        // if threadKey is now blocked, return
        return;
    }
}

int thread_stop(Thread* thread) {
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
