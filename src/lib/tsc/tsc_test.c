#include "lib/tsc/tsc.h"

#include <cpuid.h>
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
    Tsc tsc = {.cyclesPerSecond = cyclesPerSecond};
    uint64_t rax = 0, rdx = 0, rip = 0;
    emulate_fn(&tsc, &rax, &rdx, &rip, nanos);
    return (rdx << 32) | rax;
}

void emulateGivesExpectedCycles(void* unusedFixture, gconstpointer user_data) {
    void (*emulate_fn)(
        const Tsc* tsc, uint64_t* rax, uint64_t* rdx, uint64_t* rip, uint64_t nanos) = user_data;
    const uint64_t cyclesPerSecondForOneGHz = 1000000000;

    // Single ns granularity @ 1 GHz
    g_assert_cmpint(_getEmulatedCycles(emulate_fn, cyclesPerSecondForOneGHz, 1), ==, 1);

    // 1000x clock rate
    g_assert_cmpint(_getEmulatedCycles(emulate_fn, 1000 * cyclesPerSecondForOneGHz, 1), ==, 1000);

    // 1000x nanos
    g_assert_cmpint(_getEmulatedCycles(emulate_fn, cyclesPerSecondForOneGHz, 1000), ==, 1000);

    // Correct (no overflow) for 1 year @ 10 GHz
    const uint64_t oneYearInSeconds = 1L     // years
                                      * 365L // days
                                      * 24L  // hours
                                      * 60L  // minutes
                                      * 60L; // seconds
    uint64_t expectedCycles;
    gboolean ok =
        g_uint64_checked_mul(&expectedCycles, oneYearInSeconds, 10 * cyclesPerSecondForOneGHz);
    g_assert(ok);
    g_assert_cmpint(_getEmulatedCycles(
                        emulate_fn, 10L * cyclesPerSecondForOneGHz, oneYearInSeconds * 1000000000L),
                    ==, expectedCycles);
}

void closeToNativeRdtsc(void* unusedFixture, gconstpointer user_data) {
    void (*emulate_fn)(
        const Tsc* tsc, uint64_t* rax, uint64_t* rdx, uint64_t* rip, uint64_t nanos) = user_data;

    // This test is inherently flaky on high-load machines.
    // Give multiple chances.
    for (int i = 0; i < 10; ++i) {
        const uint64_t micros_delta = 100000;

        uint64_t native_t0 = __rdtsc();
        usleep(micros_delta);
        uint64_t native_t1 = __rdtsc();
        uint64_t native_delta = native_t1 - native_t0;
        trace("native_delta: %lu", native_delta);

        Tsc tsc = Tsc_init();
        uint64_t emulated_delta =
            _getEmulatedCycles(emulate_fn, tsc.cyclesPerSecond, micros_delta * 1000) -
            _getEmulatedCycles(emulate_fn, tsc.cyclesPerSecond, 0);
        trace("emulated_delta: %lu", emulated_delta);

        int64_t milliPercentDiff =
            llabs((int64_t)native_delta - (int64_t)emulated_delta) * 100L * 1000L / native_delta;
        trace("milliPercentDiff %ld", milliPercentDiff);

        // 1%
        if (milliPercentDiff < 1000) {
            // Test passes
            return;
        }

        warning("milliPercentDiff: %ld: native:%lu emulated:%lu", milliPercentDiff, native_delta,
                emulated_delta);
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

    g_test_add("/tsc/emulateRdtscGivesExpectedCycles", void, &Tsc_emulateRdtsc, NULL,
               emulateGivesExpectedCycles, NULL);
    g_test_add("/tsc/emulateRdtscpGivesExpectedCycles", void, &_emulateRdtscpWrapper, NULL,
               emulateGivesExpectedCycles, NULL);

    return g_test_run();
}
