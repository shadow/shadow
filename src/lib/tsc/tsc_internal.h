#ifndef MAIN_HOST_TSC_H
#define MAIN_HOST_TSC_H

#include <inttypes.h>
#include <stdbool.h>
#include <sys/user.h>

/*
 * Emulates an x86-64 processor's timestamp counter, as read by rdtsc and
 * rdtscp.
 */

// Returns the host system's native TSC rate, or 0 if it couldn't be found.
//
// WARNING: this is known to fail completely on some supported CPUs
// (particularly AMD), and can return the wrong value for others. i.e. this
// needs more work if we need to dependably get the host's TSC rate.
// e.g. see https://github.com/shadow/shadow/issues/1519.
uint64_t TscC_nativeCyclesPerSecond();

#endif
