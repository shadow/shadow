/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_LIB_SHIM_SHIM_RDTSC_H_
#define SRC_LIB_SHIM_SHIM_RDTSC_H_

// Initialize a signal handler function for rdtsc and rdtscp instructions.
void shim_rdtsc_init();

#endif // SRC_LIB_SHIM_SHIM_RDTSC_H_
