#ifndef MAIN_HOST_TSC_H
#define MAIN_HOST_TSC_H

#include <inttypes.h>
#include <stdbool.h>
#include <sys/user.h>

/*
 * Emulates an x86-64 processor's timestamp counter, as read by rdtsc and
 * rdtscp.
 */

typedef struct _Tsc {
    uint64_t cyclesPerSecond;
} Tsc;

// Returns the host system's native TSC rate, or 0 if it couldn't be found.
//
// WARNING: this is known to fail completely on some supported CPUs
// (particularly AMD), and can return the wrong value for others. i.e. this
// needs more work if we need to dependably get the host's TSC rate.
// e.g. see https://github.com/shadow/shadow/issues/1519.
uint64_t Tsc_nativeCyclesPerSecond();

// Instantiate a TSC with the given clock rate.
Tsc Tsc_create(uint64_t cyclesPerSecond);

// Updates `regs` to reflect the result of executing an rdtsc instruction at
// time `nanos`.
void Tsc_emulateRdtsc(const Tsc* tsc, uint64_t* rax, uint64_t* rdx, uint64_t* rip, uint64_t nanos);

// Updates `regs` to reflect the result of executing an rdtscp instruction at
// time `nanos`.
void Tsc_emulateRdtscp(const Tsc* tsc, uint64_t* rax, uint64_t* rdx, uint64_t* rcx, uint64_t* rip,
                       uint64_t nanos);

// Whether `buf` begins with an rdtsc instruction.
static inline bool isRdtsc(const uint8_t* buf) { return buf[0] == 0x0f && buf[1] == 0x31; }

// Whether `buf` begins with an rdtscp instruction.
static inline bool isRdtscp(const uint8_t* buf) {
    return buf[0] == 0x0f && buf[1] == 0x01 && buf[2] == 0xf9;
}

#endif
