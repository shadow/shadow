#include "main/host/tsc.h"

#include <assert.h>
#include <cpuid.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

#include "lib/logger/logger.h"

// Use compiler intrinsic
#define rdtscp __rdtscp

Tsc Tsc_measure() {
    unsigned int a=0,b=0,c=0,d=0;
    // Use the cpuid instruction (wrapped by __get_cpuid) to determine the clock
    // frequency. See "cpuid" in "Intel® 64 and IA-32 Architectures Software
    // Developer’s Manual Volume 2A".

    // Since we don't have an efficient way of trapping and emulating cpuid
    // to just dictate the perceived clock frequency to the managed program,
    // we need to use cpuid ourselves to figure out the clock frequency, so that
    // we can have the TSC tick at the expected rate when compared to the simulated
    // time retrieved by other means (e.g. clock_gettime).

    if (!__get_cpuid(0x0, &a, &b, &c, &d)) {
        panic("cpuid");
    }
    const unsigned int max_level = a;

    if (max_level < 0x15) {
        panic("cpuid 0x15 unsupported; can't get tsc frequency");
    }

    // cpuid 0x15 gives us 
    if (!__get_cpuid(0x15, &a, &b, &c, &d)) {
        panic("cpuid");
    }
    // From "cpuid": "An unsigned integer which is the denominator of the
    // TSC/'core crystal clock' ratio."
    const unsigned int denominator = a;
    if (!a) {
        panic("Couldn't get frequency denominator");
    }
    // From "cpuid": "An unsigned integer which is the numerator of the
    // TSC/'core crystal clock' ratio."
    const unsigned int numerator = b;
    if (!a) {
        panic("Couldn't get frequency numerator");
    }
    // From "cpuid": "An unsigned integer which is the nominal frequency of the
    // core crystal clock in Hz."
    unsigned int core = c;
    if (core) {
        Tsc tsc = {(uint64_t)c*b/a};
        debug("Calculated %" PRIu64 " cyclesPerSecond via cpuid 15h", tsc.cyclesPerSecond);
        return tsc;
    }

    // From "cpuid": "If ECX is 0, the nominal core crystal clock frequency
    // is not enumerated". Gee, thanks.
    //
    // "Intel® 64 and IA-32 ArchitecturesSoftware Developer’s Manual
    // Volume 3B: System Programming Guide, Part 2", "18.18 COUNTING CLOCKS",
    // gives a 2 row table for this case:

    // 6th and 7th generation Intel® Core™ processors -> 24 MHz
    // 
    // Next Generation Intel® Atom™ processors based on Goldmont
    // Microarchitecture with CPUID signature 06_5CH -> 19.2 MHz.
    //
    // This probably *would* be the best way to proceed, but I'm not sure precisely
    // what's meant by "CPUID signature 06_5CH".
    //
    // Instead, going back to the CPUID documentation, there's a way to get
    // the "brand string", which includes the CPU base frequency. See "The
    // Processor Brand String Method".

    // For posterity; if we were able to determine we were in the 24 MHz case
    // and not the 19.2 MHz case:
#if 0
    {
        Tsc tsc = {(uint64_t)24000000*b/a};
        debug("Calculated %" PRIu64 " cyclesPerSecond via cpuid 15h", tsc.cyclesPerSecond);
        return tsc;
    }
#endif
    
    if (!__get_cpuid(0x80000000, &a, &b, &c, &d)) {
        panic("cpuid");
    }
    if (!(a & 0x80000000)) {
        // This *shouldn't* happen. The docs say this method is supported on
        // "all Intel 64 and IA-32 processors."
        panic("Brand string method unsupported. Out of fallbacks for getting frequency.");
    }
    union {
        uint32_t ints[3][4];
        char chars[12*4];
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
    assert(brand_string.chars[sizeof(brand_string)-1] == '\0');

    trace("Got brand string %s", brand_string.chars);

    // Docs say to reverse scan for a 'blank', last token should always be of
    // form x.yz(MHz|GHz|THz).

    char* last_token = rindex(brand_string.chars, ' ');
    assert(last_token);
    assert(*last_token == ' ');

    float frequency;
    char scale_c;
    if (sscanf(last_token, " %f%cHz", &frequency, &scale_c) != 2) {
        panic("Couldn't parse %s", last_token);
    }
    uint64_t scale = 0;
    if (scale_c == 'M') {
        scale = 1000000ull;
    } else if (scale_c == 'G') {
        scale = 1000000000ull;
    } else if (scale_c == 'T') {
        scale = 1000000000000ull;
    } else {
        panic("Unrecognized scale character %c", scale_c);
    }

    Tsc tsc = {frequency*scale};
    debug("Calculated %" PRIu64 " cyclesPerSecond via brand string", tsc.cyclesPerSecond);
    return tsc;
}

static void _Tsc_setRdtscCycles(const Tsc* tsc, struct user_regs_struct* regs,
                                uint64_t nanos) {
    // Guaranteed not to overflow since the operands are both 64 bit.
    __uint128_t gigaCycles = (__uint128_t)tsc->cyclesPerSecond * nanos; 
    uint64_t cycles = gigaCycles / 1000000000;
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
    rdtscp(&a);
    regs->rcx = a;
    regs->rip += 3;
}
