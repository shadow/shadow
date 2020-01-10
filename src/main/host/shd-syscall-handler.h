/*
 * shd-syscall-handler.h
 *
 *  Created on: Dec 26, 2019
 *      Author: rjansen
 */

#ifndef SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_
#define SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_

#include <glib.h>
#include <sys/time.h>

typedef struct _SysCallHandler SysCallHandler;

typedef struct _SysCallReturn SysCallReturn;
struct _SysCallReturn {
    gboolean block;
    int errnum;
    union {
        int _int;
        uint _uint;
        long _long;
        time_t _time_t;
        size_t _size_t;
        ssize_t _ssize_t;
        void* _void_p;

    } retval;
};

#include "main/host/shd-thread.h"
#include "main/host/process.h"
#include "main/host/host.h"

SysCallHandler* syscallhandler_new(Host* host, Process* process);
void syscallhandler_ref(SysCallHandler* sys);
void syscallhandler_unref(SysCallHandler* sys);

///////////////////////////////////////////////////////////
// System Calls
///////////////////////////////////////////////////////////

SysCallReturn syscallhandler_sleep(SysCallHandler* sys, Thread* thread,
        unsigned int sec);
SysCallReturn syscallhandler_usleep(SysCallHandler* sys, Thread* thread,
        unsigned int usec);
SysCallReturn syscallhandler_nanosleep(SysCallHandler* sys, Thread* thread,
        const struct timespec *req, struct timespec *rem);

SysCallReturn syscallhandler_time(SysCallHandler* sys, Thread* thread,
        time_t* tloc);
SysCallReturn syscallhandler_clock_gettime(SysCallHandler* sys, Thread* thread,
        clockid_t clk_id, struct timespec *tp);
SysCallReturn syscallhandler_clock_getres(SysCallHandler* sys, Thread* thread,
        clockid_t clk_id, struct timespec *res);
SysCallReturn syscallhandler_gettimeofday(SysCallHandler* sys, Thread* thread,
        struct timeval *tv, struct timezone *tz);

#endif /* SRC_MAIN_HOST_SHD_SYSCALL_HANDLER_H_ */
