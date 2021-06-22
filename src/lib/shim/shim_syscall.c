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
#include "lib/shim/shim_syscall.h"
#include "main/core/support/definitions.h" // for SIMTIME definitions

// We store the simulation time using timespec to reduce the number of
// conversions that we need to do while servicing syscalls.
static struct timespec _cached_simulation_time = {0};

void shim_syscall_set_simtime_nanos(uint64_t simulation_nanos) {
    _cached_simulation_time.tv_sec = simulation_nanos / SIMTIME_ONE_SECOND;
    _cached_simulation_time.tv_nsec = simulation_nanos % SIMTIME_ONE_SECOND;
}

uint64_t shim_syscall_get_simtime_nanos() {
    return (uint64_t)(_cached_simulation_time.tv_sec * SIMTIME_ONE_SECOND) +
           _cached_simulation_time.tv_nsec;
}

static struct timespec* _shim_syscall_get_time() {
    // First try to get time from shared mem.
    struct timespec* simtime_ts = shim_get_shared_time_location();

    // If that's unavailable, check if the time has been cached before.
    if (simtime_ts == NULL) {
        simtime_ts = &_cached_simulation_time;
    }

    // If the time is not set, then we fail.
    if (simtime_ts->tv_sec == 0 && simtime_ts->tv_nsec == 0) {
        return NULL;
    }

#ifdef DEBUG
    if (simtime_ts == &_cached_simulation_time) {
        trace("simtime is available in the shim using cached time");
    } else {
        trace("simtime is available in the shim using shared memory");
    }
#endif

    return simtime_ts;
}

bool shim_syscall(long syscall_num, long* rv, va_list args) {
    // This function is called on every syscall operation so be careful not to doing
    // anything too expensive outside of the switch cases.
    struct timespec* simtime_ts;

    switch (syscall_num) {
        case SYS_clock_gettime: {
            // We can handle it if the time is available.
            if (!(simtime_ts = _shim_syscall_get_time())) {
                return false;
            }

            trace("servicing syscall %ld:clock_gettime from the shim", syscall_num);

            clockid_t clk_id = va_arg(args, clockid_t);
            struct timespec* tp = va_arg(args, struct timespec*);

            if (tp) {
                *tp = *simtime_ts;
                trace("clock_gettime() successfully copied time");
                *rv = 0;
            } else {
                trace("found NULL timespec pointer in clock_gettime");
                *rv = -EFAULT;
            }

            break;
        }

        case SYS_time: {
            // We can handle it if the time is available.
            if (!(simtime_ts = _shim_syscall_get_time())) {
                return false;
            }

            trace("servicing syscall %ld:time from the shim", syscall_num);

            time_t* tp = va_arg(args, time_t*);

            if (tp) {
                *tp = simtime_ts->tv_sec;
                trace("time() successfully copied time");
            }
            *rv = simtime_ts->tv_sec;

            break;
        }

        case SYS_gettimeofday: {
            // We can handle it if the time is available.
            if (!(simtime_ts = _shim_syscall_get_time())) {
                return false;
            }

            trace("servicing syscall %ld:gettimeofday from the shim", syscall_num);

            struct timeval* tp = va_arg(args, struct timeval*);

            if (tp) {
                tp->tv_sec = simtime_ts->tv_sec;
                tp->tv_usec = simtime_ts->tv_nsec / SIMTIME_ONE_MICROSECOND;
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