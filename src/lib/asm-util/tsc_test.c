#include "lib/tsc/tsc.h"

#include <cpuid.h>
#include <errno.h>
#include <glib.h>
#include <inttypes.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <x86intrin.h>

#include "lib/logger/logger.h"

static uint64_t _getEmulatedCycles(void (*emulate_fn)(const Tsc* tsc, uint64_t* rax, uint64_t* rdx,
                                                      uint64_t* rip, uint64_t nanos),
                                   uint64_t cyclesPerSecond, int64_t nanos) {
    Tsc tsc = Tsc_create(cyclesPerSecond);
    uint64_t rax = 0, rdx = 0, rip = 0;
    emulate_fn(&tsc, &rax, &rdx, &rip, nanos);
    return (rdx << 32) | rax;
}

static struct timespec _timespecsub(const struct timespec* t1, const struct timespec* t2) {
    struct timespec res;
    res.tv_sec = t1->tv_sec - t2->tv_sec;
    res.tv_nsec = t1->tv_nsec - t2->tv_nsec;
    if (res.tv_nsec < 0) {
        res.tv_nsec += 1000000000L;
        --res.tv_sec;
    }
    return res;
}

void closeToNativeRdtsc(void* unusedFixture, gconstpointer user_data) {
    void (*emulate_fn)(
        const Tsc* tsc, uint64_t* rax, uint64_t* rdx, uint64_t* rip, uint64_t nanos) = user_data;

    Tsc tsc = Tsc_create(Tsc_nativeCyclesPerSecond());

    // Use the monotonic timer.
    clockid_t clk_id = CLOCK_MONOTONIC;

    // This test is inherently flaky on high-load machines.
    // Give multiple chances.
    for (int i = 0; i < 10; ++i) {
        struct timespec t0, t1;
        if (clock_gettime(clk_id, &t0)) {
            panic("clock_gettime: %s", strerror(errno));
        }
        uint64_t t0_cycles = __rdtsc();

        // Sleep for half of a second. We're not bothering to try accounting for
        // the overhead of clock_gettime etc itself; using a relatively long
        // interval helps amortize it.
        usleep(500000);

        uint64_t t1_cycles = __rdtsc();
        if (clock_gettime(clk_id, &t1)) {
            panic("clock_gettime: %s", strerror(errno));
        }

        struct timespec delta = _timespecsub(&t1, &t0);
        trace("Real dt: %ld.%09ld", delta.tv_sec, delta.tv_nsec);
        uint64_t actual_dcycles = t1_cycles - t0_cycles;
        trace("Real # cycles: %ld", actual_dcycles);

        uint64_t emulated_dcycles = _getEmulatedCycles(emulate_fn, tsc.cyclesPerSecond,
                                                       delta.tv_sec * 1000000000L + delta.tv_nsec) -
                                    _getEmulatedCycles(emulate_fn, tsc.cyclesPerSecond, 0);
        trace("Emulated # cycles: %lu", emulated_dcycles);
        uint64_t ddcycles = llabs((int64_t)emulated_dcycles - (int64_t)actual_dcycles);
        double percent_error = (double)ddcycles * 100 / actual_dcycles;
        trace("ddcycles: %lu error:%f%%", ddcycles, percent_error);
        if (percent_error < 1) {
            return;
        }
    }
    logger_flush(logger_getDefault());
    g_test_fail();
}

// Compatibility wrapper that ignores emulation of rcx register, allowing a single test function
// to validate just the rax and rdx (timestamp) output of rdtscp.
static void _emulateRdtscpWrapper(const Tsc* tsc, uint64_t* rax, uint64_t* rdx, uint64_t* rip,
                                  uint64_t nanos) {
    uint64_t rcx;
    Tsc_emulateRdtscp(tsc, rax, rdx, &rcx, rip, nanos);
}

static bool _hostHasInvariantTimer() {
    // Intel manual 17.17.4 Invariant Time-Keeping:
    // "If CPUID.15H:EBX[31:0] != 0 and CPUID.80000007H:EDX[InvariantTSC] = 1,
    // the following linearity relationship holds between TSC and the ART
    // hardware..."
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (!__get_cpuid(0x15, &a, &b, &c, &d)) {
        warning("cpuid 0x15 failed");
        return false;
    }
    if (!b) {
        debug("cpuid.15h:EBX == 0; no invariant TSC");
        return false;
    }
    if (!__get_cpuid(0x80000007, &a, &b, &c, &d)) {
        warning("cpuid 0x0x80000007 failed");
        return false;
    }
    trace("cpuid 0x80000007 returned edx:%x", d);
    if (!(d & (1 << 8))) {
        warning("invariant tsc flag not set");
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    // Define the tests.

    // Can only meaningfully compare to the host tsc if the host cpu implements
    // invariant tsc (rdtsc always at base cpu frequency).
    if (_hostHasInvariantTimer()) {
        g_test_add(
            "/tsc/rdtscIsCloseToNative", void, &Tsc_emulateRdtsc, NULL, closeToNativeRdtsc, NULL);
        g_test_add("/tsc/rdtscpIsCloseToNative", void, &_emulateRdtscpWrapper, NULL,
                   closeToNativeRdtsc, NULL);
    }

    return g_test_run();
}
