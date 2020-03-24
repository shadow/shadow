#include "tsc.h"

#include <glib.h>
#include <inttypes.h>
#include <locale.h>
#include <stdint.h>

void measureGivesConsistentResults() {
    Tsc baseline = Tsc_measure();
    // FIXME: this is a pretty loose consistency bound, but tighter bounds
    // currently occasionally fail. The right thing to do is extract the
    // nominal rate via cpuid, and have a test validating that is within the
    // ballpark of the measured approach.
    for (int i = 0; i < 100; ++i) {
        Tsc test = Tsc_measure();
        int64_t milliPercentDiff = llabs((int64_t)test.cyclesPerSecond -
                                         (int64_t)baseline.cyclesPerSecond) *
                                   100L * 1000L / baseline.cyclesPerSecond;
        /* 1.000% */
        g_assert_cmpint(milliPercentDiff, <, 1000);
    }
    g_assert_true(TRUE);
}

static uint64_t _getEmulatedCycles(
    void (*emulate_fn)(const Tsc* tsc, struct user_regs_struct* regs,
                       uint64_t nanos),
    uint64_t cyclesPerSecond, int64_t nanos) {

    Tsc tsc = {.cyclesPerSecond = cyclesPerSecond};
    struct user_regs_struct regs = {};
    emulate_fn(&tsc, &regs, nanos);
    return (regs.rdx << 32) | regs.rax;
}

void emulateGivesExpectedCycles(void* unusedFixture, gconstpointer user_data) {
    void (*emulate_fn)(const Tsc* tsc,
                       struct user_regs_struct* regs,
                       uint64_t nanos) = user_data;
    const uint64_t cyclesPerSecondForOneGHz = 1000000000;

    // Single ns granularity @ 1 GHz
    g_assert_cmpint(
        _getEmulatedCycles(emulate_fn, cyclesPerSecondForOneGHz, 1), ==, 1);

    // 1000x clock rate
    g_assert_cmpint(
        _getEmulatedCycles(emulate_fn, 1000 * cyclesPerSecondForOneGHz, 1),
        ==,
        1000);

    // 1000x nanos
    g_assert_cmpint(
        _getEmulatedCycles(emulate_fn, cyclesPerSecondForOneGHz, 1000),
        ==,
        1000);

    // Correct (no overflow) for 1 year @ 10 GHz
    const uint64_t oneYearInSeconds = 1L     // years
                                      * 365L // days
                                      * 24L  // hours
                                      * 60L  // minutes
                                      * 60L; // seconds
    uint64_t expectedCycles;
    gboolean ok = g_uint64_checked_mul(
        &expectedCycles, oneYearInSeconds, 10 * cyclesPerSecondForOneGHz);
    g_assert(ok);
    g_assert_cmpint(_getEmulatedCycles(emulate_fn,
                                       10L * cyclesPerSecondForOneGHz,
                                       oneYearInSeconds * 1000000000L),
                    ==,
                    expectedCycles);
}

int main(int argc, char* argv[]) {
    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    // Define the tests.

    // FIXME: flaky
    //g_test_add_func("/tsc/measureGivesConsistentResults",
    //                measureGivesConsistentResults);

    g_test_add("/tsc/emulateRdtscGivesExpectedCycles",
               void,
               &Tsc_emulateRdtsc,
               NULL,
               emulateGivesExpectedCycles,
               NULL);
    g_test_add("/tsc/emulateRdtscpGivesExpectedCycles",
               void,
               &Tsc_emulateRdtscp,
               NULL,
               emulateGivesExpectedCycles,
               NULL);

    return g_test_run();
}
