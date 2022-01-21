/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>

#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_sys.h"
#include "main/core/support/definitions.h" // for SIMTIME definitions

static _Atomic EmulatedTime* _shim_sys_get_time() {
    // First try to get time from shared mem.
    _Atomic EmulatedTime* simtime_ts = shim_get_shared_time_location();

    // If that's unavailable, fail. This can happen during early init.
    if (simtime_ts == NULL) {
        return NULL;
    }

    return simtime_ts;
}

uint64_t shim_sys_get_simtime_nanos() {
    _Atomic EmulatedTime* ts = _shim_sys_get_time();
    if (!ts) {
        return 0;
    }

    return *ts;
}

bool shim_sys_handle_syscall_locally(long syscall_num, long* rv, va_list args) {
    // This function is called on every syscall operation so be careful not to doing
    // anything too expensive outside of the switch cases.

    switch (syscall_num) {
        case SYS_clock_gettime: {
            EmulatedTime emulated_time;
            {
                _Atomic EmulatedTime* emulated_time_p = NULL;
                // We can handle it if the time is available.
                if (!(emulated_time_p = _shim_sys_get_time())) {
                    return false;
                }
                emulated_time = *emulated_time_p;
            }

            trace("servicing syscall %ld:clock_gettime from the shim", syscall_num);

            clockid_t clk_id = va_arg(args, clockid_t);
            struct timespec* tp = va_arg(args, struct timespec*);

            if (tp) {
                *tp = (struct timespec){
                    .tv_sec = emulated_time / SIMTIME_ONE_SECOND,
                    .tv_nsec = emulated_time % SIMTIME_ONE_SECOND,
                };
                trace("clock_gettime() successfully copied time");
                *rv = 0;
            } else {
                trace("found NULL timespec pointer in clock_gettime");
                *rv = -EFAULT;
            }

            break;
        }

        case SYS_time: {
            EmulatedTime emulated_time;
            {
                _Atomic EmulatedTime* emulated_time_p = NULL;
                // We can handle it if the time is available.
                if (!(emulated_time_p = _shim_sys_get_time())) {
                    return false;
                }
                emulated_time = *emulated_time_p;
            }
            time_t now = emulated_time / SIMTIME_ONE_SECOND;

            trace("servicing syscall %ld:time from the shim", syscall_num);

            time_t* tp = va_arg(args, time_t*);

            if (tp) {
                *tp = now;
                trace("time() successfully copied time");
            }
            *rv = now;

            break;
        }

        case SYS_gettimeofday: {
            EmulatedTime emulated_time;
            {
                _Atomic EmulatedTime* emulated_time_p = NULL;
                // We can handle it if the time is available.
                if (!(emulated_time_p = _shim_sys_get_time())) {
                    return false;
                }
                emulated_time = *emulated_time_p;
            }
            uint64_t micros = emulated_time / SIMTIME_ONE_MICROSECOND;

            trace("servicing syscall %ld:gettimeofday from the shim", syscall_num);

            struct timeval* tp = va_arg(args, struct timeval*);

            if (tp) {
                tp->tv_sec = micros / 1000000;
                tp->tv_usec = micros % 1000000;
                trace("gettimeofday() successfully copied time");
            }
            *rv = 0;

            break;
        }

        default: {
            // the syscall was not handled
            return false;
        }
    }

    // the syscall was handled
    return true;
}
