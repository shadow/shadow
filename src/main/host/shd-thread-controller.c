/*
 * shd-thread-controller.c
 *
 *  Created on: Dec 13, 2019
 *      Author: rjansen
 */
#include "main/host/shd-thread-controller.h"

#include "main/host/shd-syscall-handler.h"

struct _ThreadControlBlock {
    // needs to store comm channel state, etc.

    SystemCallHandler* sys;

    int isAlive;

    gint referenceCount;
    MAGIC_DECLARE;
};

ThreadControlBlock* threadcontroller_new(SystemCallHandler* sys) {
    ThreadControlBlock* tcb = g_new0(ThreadControlBlock, 1);
    MAGIC_INIT(tcb);

    tcb->sys = sys;
    syscallhandler_ref(sys);

    tcb->referenceCount = 1;

    // tcb has access to a global, thread safe shared memory manager

    // this function is called when the process is created at the beginning
    // of the sim. but the process may not launch/start until later. any
    // resources for launch/start should be allocated in the respective funcs.

    return tcb;
}

static void _threadcontroller_free(ThreadControlBlock* tcb) {
    MAGIC_ASSERT(tcb);

    if(tcb->sys) {
        syscallhandler_unref(tcb->sys);
    }

    MAGIC_CLEAR(tcb);
    g_free(tcb);
}

void threadcontroller_ref(ThreadControlBlock* tcb) {
    MAGIC_ASSERT(tcb);
    (tcb->referenceCount)++;
}

void threadcontroller_unref(ThreadControlBlock* tcb) {
    MAGIC_ASSERT(tcb);
    (tcb->referenceCount)--;
    utility_assert(tcb->referenceCount >= 0);
    if(tcb->referenceCount == 0) {
        _threadcontroller_free(tcb);
    }
}

static void _threadcontroller_launch(ThreadControlBlock* tcb) {
    MAGIC_ASSERT(tcb);
    tcb->isAlive = 1;
    // launch the plugin process, set up the comm channel, get the
    // process to the point where it blocks before calling main()
}

void threadcontroller_start(ThreadControlBlock* tcb, int argc, char** argv) {
    MAGIC_ASSERT(tcb);

    _threadcontroller_launch(tcb);

    // call main()
    // the plugin will run until it makes a blocking call
    // return
}

void threadcontroller_continue(ThreadControlBlock* tcb) {
    MAGIC_ASSERT(tcb);

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
                unsigned int result = syscallhandler_sleep(tcb->sys, threadKey, sec);

                // if return variable indicates that the call is blocked
                // we need to block the plugin, i.e. mark thread_key as blocked
                // and then move to the next thread
                // TODO how to mark threadKey as blocked?

                break;
            }

            case 2: {
                unsigned int usec = 1;
                int result = syscallhandler_usleep(tcb->sys, threadKey, usec);
                break;
            }

            case 3: {
                struct timespec req;
                struct timespec rem;
                int result = syscallhandler_nanosleep(tcb->sys, threadKey, &req, &rem);
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

int threadcontroller_stop(ThreadControlBlock* tcb) {
    MAGIC_ASSERT(tcb);

    // if the proc has already stopped, just return the returncode

    // terminate the process (send a signal that will cause it to
    // call proper destructors, etc)

    tcb->isAlive = 0;

    // return the return code of the process
    return 0;
}

gboolean threadcontroller_isAlive(ThreadControlBlock* tcb) {
    MAGIC_ASSERT(tcb);
    // TODO
    // return TRUE if at least one thread is still running
    // return false if the process died or completed
    return tcb->isAlive;
}
