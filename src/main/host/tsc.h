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

// Initializes a Tsc heuristically by measuring on the host system. FIXME:
// should be able to do this more efficiently and accurately by querying the
// CPU.
Tsc Tsc_measure();

// Updates `regs` to reflect the result of executing an rdtsc instruction at
// time `nanos`.
void Tsc_emulateRdtsc(const Tsc* tsc, struct user_regs_struct* regs,
                      uint64_t nanos);

// Updates `regs` to reflect the result of executing an rdtscp instruction at
// time `nanos`.
void Tsc_emulateRdtscp(const Tsc* tsc, struct user_regs_struct* regs,
                       uint64_t nanos);

// Whether `buf` begins with an rdtsc instruction.
static inline bool isRdtsc(const uint8_t* buf) {
    return buf[0] == 0x0f && buf[1] == 0x31;
}

// Whether `buf` begins with an rdtscp instruction.
static inline bool isRdtscp(const uint8_t* buf) {
    return buf[0] == 0x0f && buf[1] == 0x01 && buf[2] == 0xf9;
}

#endif
