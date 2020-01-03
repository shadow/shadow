/*
 * shd-syscall-handler.c
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */
#include "main/host/shd-syscall-handler.h"

struct _SysCallHandler {
    Host* host;
    Process* process;
    int referenceCount;

    MAGIC_DECLARE;
};

SysCallHandler* syscallhandler_new(Host* host, Process* process) {
    SysCallHandler* sys = g_new0(SysCallHandler, 1);
    MAGIC_INIT(sys);

    sys->host = host;
    host_ref(host);
    sys->process = process;
    process_ref(process);

    sys->referenceCount = 1;

    return sys;
}

static void _syscallhandler_free(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);

    if(sys->host) {
        host_unref(sys->host);
    }
    if(sys->process) {
        process_unref(sys->process);
    }

    MAGIC_CLEAR(sys);
    g_free(sys);
}

void syscallhandler_ref(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    (sys->referenceCount)++;
}

void syscallhandler_unref(SysCallHandler* sys) {
    MAGIC_ASSERT(sys);
    (sys->referenceCount)--;
    utility_assert(sys->referenceCount >= 0);
    if(sys->referenceCount == 0) {
        _syscallhandler_free(sys);
    }
}

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

unsigned int syscallhandler_sleep(SysCallHandler* sys, Thread* thread, gboolean* block,
        unsigned int sec) {
    // TODO implement
    return 0;
}

int syscallhandler_usleep(SysCallHandler* sys, Thread* thread, gboolean* block,
        unsigned int usec) {
    // TODO implement
    return 0;
}

int syscallhandler_nanosleep(SysCallHandler* sys, Thread* thread, gboolean* block,
        const struct timespec *req, struct timespec *rem) {
    // TODO implement
    return 0;
}
