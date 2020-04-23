#include "main/host/tsc.h"

#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

// Assumes lhs >= rhs
void _timespec_sub(struct timespec* res, const struct timespec* lhs,
                   const struct timespec* rhs) {
    int64_t sec = (int64_t)lhs->tv_sec - (int64_t)rhs->tv_sec;
    int64_t nsec = (int64_t)lhs->tv_nsec - (int64_t)rhs->tv_nsec;
    if (nsec < 0) {
        // Borrow
        --sec;
        nsec += 1000000000L;
    }
    g_assert(sec >= 0);
    g_assert(nsec >= 0);
    res->tv_sec = sec;
    res->tv_nsec = nsec;
}

static int64_t _timespec_toNanos(const struct timespec* ts) {
    uint64_t nanos;
    gboolean ok;
    ok = g_uint64_checked_mul(&nanos, ts->tv_sec, 1000000000UL);
    g_assert(ok);
    ok = g_uint64_checked_add(&nanos, ts->tv_nsec, nanos);
    g_assert(ok);
    return nanos;
}

Tsc Tsc_measure() {
    Tsc tsc = {.cyclesPerSecond = UINT64_MAX};
    for (int i = 0; i < 10; ++i) {
        struct timespec ts_start;
        unsigned int unused;
        if (clock_gettime(CLOCK_MONOTONIC, &ts_start) < 0) {
            g_error("clock_gettime: %s", strerror(errno));
        }
        uint64_t rdtsc_start = __rdtscp(&unused);

        usleep(1000);

        struct timespec ts_end;
        if (clock_gettime(CLOCK_MONOTONIC, &ts_end) < 0) {
            g_error("clock_gettime: %s", strerror(errno));
        }

        uint64_t rdtsc_end = __rdtscp(&unused);
        uint64_t cycles = rdtsc_end - rdtsc_start;
        struct timespec ts_diff;
        _timespec_sub(&ts_diff, &ts_end, &ts_start);
        uint64_t ns = _timespec_toNanos(&ts_diff);
        tsc.cyclesPerSecond =
            MIN(tsc.cyclesPerSecond, cycles * 1000000000 / ns);
    }
    g_info("Calculated %" PRIu64 " cyclesPerSecond", tsc.cyclesPerSecond);

    return tsc;
}

// FIXME: This isn't very efficient. Probably the right way to do this is to
// just find some existing numeric routing for doing a 128-bit multiply.
static void _Tsc_setRdtscCycles(const Tsc* tsc, struct user_regs_struct* regs,
                                uint64_t nanos) {
    const uint64_t maxCycles = UINT64_MAX;
    const uint64_t maxSeconds = maxCycles / tsc->cyclesPerSecond;
    // Unhandled: there's no way to represent the result in 64 bits.  A real
    // processor would probably cope with this somehow (e.g. wrap around and
    // carry on), but it's unlikely to be what we want in a simulation.
    g_assert(nanos / 1000000000 < maxSeconds);

    const uint64_t maxGigaCyclesAtOnce = UINT64_MAX;
    const uint64_t maxNanosAtOnce = maxGigaCyclesAtOnce / tsc->cyclesPerSecond;

    uint64_t gigaCyclesForMaxNanosAtOnce;
    if (!g_uint64_checked_mul(&gigaCyclesForMaxNanosAtOnce,
                              maxNanosAtOnce,
                              tsc->cyclesPerSecond)) {
        g_assert_not_reached();
    }
    uint64_t cyclesForMaxNanosAtOnce = gigaCyclesForMaxNanosAtOnce / 1000000000;

    uint64_t accountedCycles = 0;
    while (nanos > maxNanosAtOnce) {
        nanos -= maxNanosAtOnce;
        accountedCycles += cyclesForMaxNanosAtOnce;
    }

    uint64_t remainingGigaCycles;
    if (!g_uint64_checked_mul(
            &remainingGigaCycles, nanos, tsc->cyclesPerSecond)) {
        g_assert_not_reached();
    }
    const uint64_t cycles =
        accountedCycles + (remainingGigaCycles / 1000000000);
    regs->rdx = (cycles >> 32) & 0xffffffff;
    regs->rax = cycles & 0xffffffff;
}

void Tsc_emulateRdtsc(const Tsc* tsc, struct user_regs_struct* regs,
                      uint64_t nanos) {
    _Tsc_setRdtscCycles(tsc, regs, nanos);
    regs->rip += 2;
}

void Tsc_emulateRdtscp(const Tsc* tsc, struct user_regs_struct* regs,
                       uint64_t nanos) {
    _Tsc_setRdtscCycles(tsc, regs, nanos);
    // FIXME: using the real instruction to put plausible data int rcx, but we
    // probably want an emulated value. It's some metadata about the processor,
    // including the processor ID.
    unsigned int a;
    __rdtscp(&a);
    regs->rcx = a;
    regs->rip += 3;
}
