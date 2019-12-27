/*
 * shd-syscall-handler.c
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */
#include "main/host/shd-syscall-handler.h"

struct _SystemCallHandler {
    Host* host;
    int referenceCount;

    MAGIC_DECLARE;
};

SystemCallHandler* syscallhandler_new(Host* host) {
    SystemCallHandler* sys = g_new0(SystemCallHandler, 1);
    MAGIC_INIT(sys);

    sys->host = host;
    host_ref(host);

    sys->referenceCount = 1;

    return sys;
}

static void _syscallhandler_free(SystemCallHandler* sys) {
    MAGIC_ASSERT(sys);

    if(sys->host) {
        host_unref(sys->host);
    }

    MAGIC_CLEAR(sys);
    g_free(sys);
}

void syscallhandler_ref(SystemCallHandler* sys) {
    MAGIC_ASSERT(sys);
    (sys->referenceCount)++;
}

void syscallhandler_unref(SystemCallHandler* sys) {
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

unsigned int syscallhandler_sleep(SystemCallHandler* sys, int threadKey,
        unsigned int sec) {
    // TODO implement
    return 0;
}

int syscallhandler_usleep(SystemCallHandler* sys, int threadKey,
        unsigned int usec) {
    // TODO implement
    return 0;
}

int syscallhandler_nanosleep(SystemCallHandler* sys, int threadKey,
        const struct timespec *req, struct timespec *rem) {
    // TODO implement
    return 0;
}
