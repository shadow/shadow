/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef SRC_LIB_SHIM_SHIM_INSN_EMU_H_
#define SRC_LIB_SHIM_SHIM_INSN_EMU_H_

// Arrange for special instructions to raise SIGSEGV,
// and install a SIGSEGV signal handler to emulate the instructions when that happens.
//
// These currently include:
// * rdtsc
// * rdtscp
void shim_insn_emu_init();

#endif // SRC_LIB_SHIM_SHIM_INSN_EMU_H_
