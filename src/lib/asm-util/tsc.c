#include "lib/asm-util/tsc_internal.h"

#include <assert.h>
#include <cpuid.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#include "lib/logger/logger.h"

// Returns 0 on failure.
static uint64_t _frequency_via_cpuid0x15() {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    // Use the cpuid instruction (wrapped by __get_cpuid) to determine the clock
    // frequency. See "cpuid" in "Intel® 64 and IA-32 Architectures Software
    // Developer’s Manual Volume 2A".

    if (!__get_cpuid(0x0, &a, &b, &c, &d)) {
        // Should never happen.
        panic("cpuid");
    }
    const unsigned int max_level = a;

    if (max_level < 0x15) {
        debug("cpuid 0x15 unsupported; can't get tsc frequency");
        return 0;
    }

    if (!__get_cpuid(0x15, &a, &b, &c, &d)) {
        debug("cpuid 0x15 failed");
        return 0;
    }
    // From "cpuid": "An unsigned integer which is the denominator of the
    // TSC/'core crystal clock' ratio."
    const unsigned int denominator = a;
    if (!denominator) {
        debug("cpuid 0x15 didn't give denominator");
        return 0;
    }
    // From "cpuid": "An unsigned integer which is the numerator of the
    // TSC/'core crystal clock' ratio."
    const unsigned int numerator = b;
    if (!numerator) {
        debug("cpuid 0x15 didn't give numerator");
        return 0;
    }
    // From "cpuid": "An unsigned integer which is the nominal frequency of the
    // core crystal clock in Hz."
    unsigned int core = c;
    if (!core) {
        // From "cpuid": "If ECX is 0, the nominal core crystal clock frequency
        // is not enumerated".
        //
        // The June 2021 revision of "Intel® 64 and IA-32 Architectures Software
        // Developer’s Manual Combined Volumes: 1, 2A, 2B, 2C, 2D, 3A, 3B, 3C,
        // 3D and 4", section "18.7.3" has a 3 row table for this case:
        //
        //   Intel® Xeon® Processor Scalable Family with CPUID signature 06_55H
        //   -> 25 MHz.
        //
        //   6th and 7th generation Intel® Core™ processors -> 24 MHz.
        //
        //   Next Generation Intel® Atom™ processors based on Goldmont
        //   Microarchitecture with CPUID signature 06_5CH -> 19.2 MHz.
        //
        // I wasn't able to find a precise mapping from "06_5CH" to exactly a
        // cpuid result in the Intel manual, but from
        // https://en.wikichip.org/wiki/intel/cpuid, it appears to mean "family
        // 0x6, extended model 0x5, model 0xc", as returned by cpuid 0x1.
        //
        // AFAICT from https://www.amd.com/system/files/TechDocs/25481.pdf, AMD
        // processors don't support cpuid 0x15 at all, so we would've already
        // bailed out earlier for those.
        if (!__get_cpuid(0x1, &a, &b, &c, &d)) {
            debug("cpuid 0x1 failed");
            return 0;
        }
        // bits 11-8
        unsigned int family_id = (a >> 8) & 0xf;
        // bits 19-16
        unsigned int extended_model_id = (a >> 16) & 0xf;
        // bits 7-4
        unsigned int model = (a >> 4) & 0xf;
        trace("rax %u -> family_id:0x%x extended_model_id:0x%x model:0x%x", a, family_id,
              extended_model_id, model);
        if (family_id == 0x6 && extended_model_id == 0x5 && model == 0x5) {
            trace("xeon; using 25 MHz crystal frequency");
            core = 25000000;
        } else if (family_id == 0x6 && extended_model_id == 0x5 && model == 0xc) {
            trace("goldmont; using 19.2 MHz crystal frequency");
            core = 19200000;
        } else {
            trace("non-goldmont, non-xeon; using 24 MHz crystal frequency");
            core = 24000000;
        }
    }

    uint64_t freq = (uint64_t)core * numerator / denominator;
    debug("Calculated %" PRIu64 " cyclesPerSecond via cpuid 15h", freq);
    return freq;
}

// This isn't guaranteed to be the TSC frequency, but is close. Probably better
// to just fail rather than returning something "close", but keeping this around
// for future reference for now.
//
// Returns 0 on failure.
__attribute__((unused)) static uint64_t _frequency_via_brand_string() {
    // While this *sounds* hacky at first glance, the cpuid docs provide a very
    // precise specification for parsing the cpu frequency out of the brand
    // string.

    unsigned int a = 0, b = 0, c = 0, d = 0;

    if (!__get_cpuid(0x80000000, &a, &b, &c, &d)) {
        debug("cpuid 0x80000000 failed");
        return 0;
    }
    if (!(a & 0x80000000)) {
        // This *shouldn't* happen. The docs say this method is supported on
        // "all Intel 64 and IA-32 processors."
        debug("Brand string method for getting TSC frequency unsupported.");
        return 0;
    }

    // We need to call cpuid 3 times, each time getting 4*4 bytes of the string.
    union {
        uint32_t ints[3 /*calls*/][4 /*regs*/];
        char chars[3 /*calls*/ * 4 /*regs*/ * sizeof(uint32_t)];
    } brand_string;
    for (int i = 0; i < 3; ++i) {
        if (!__get_cpuid(0x80000002 + i, &a, &b, &c, &d)) {
            panic("cpuid");
        }
        brand_string.ints[i][0] = a;
        brand_string.ints[i][1] = b;
        brand_string.ints[i][2] = c;
        brand_string.ints[i][3] = d;
    }
    // Guaranteed to be null terminated.
    assert(brand_string.chars[sizeof(brand_string) - 1] == '\0');

    trace("Got brand string %s", brand_string.chars);

    // Docs say to reverse scan for a 'blank', last token should always be of
    // form x.yz[MGT]Hz.

    char* last_token = rindex(brand_string.chars, ' ');
    assert(last_token);
    assert(*last_token == ' ');

    float base_frequency;
    char scale_c;
    if (sscanf(last_token, " %f%cHz", &base_frequency, &scale_c) != 2) {
        error("Couldn't parse brand string token %s", last_token);
        return 0;
    }
    uint64_t scale = 0;
    if (scale_c == 'M') {
        scale = 1000000ull;
    } else if (scale_c == 'G') {
        scale = 1000000000ull;
    } else if (scale_c == 'T') {
        scale = 1000000000000ull;
    } else {
        error("Unrecognized brand string scale character %c", scale_c);
        return 0;
    }

    uint64_t frequency = (uint64_t)(base_frequency * scale);
    debug("Calculated %" PRIu64 " cyclesPerSecond via brand string", frequency);
    return frequency;
}

uint64_t TscC_nativeCyclesPerSecond() {
    // Since we don't have an efficient way of trapping and emulating cpuid
    // to just dictate the perceived clock frequency to the managed program,
    // we need to use cpuid ourselves to figure out the clock frequency, so that
    // we can have the TSC tick at the expected rate when compared to the simulated
    // time retrieved by other means (e.g. clock_gettime).

    uint64_t f = _frequency_via_cpuid0x15();
    if (f) {
        return f;
    }
    // Potentially add other methods here for CPUs that don't support cpuid
    // 0x15.
    warning("Couldn't get CPU TSC frequency");
    return 0;
}
