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

// Instantiate a TSC with the same frequency as the host system TSC.
Tsc Tsc_init();

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
