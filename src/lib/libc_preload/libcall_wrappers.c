/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

/// This file contains a symbol for some libc library calls (i.e., in man
/// section 3; see `man man`) that we can redirect to an equivalent syscall.

// To get the INTERPOSE defs. Do not include other headers to avoid conflicts.
#include "interpose.h"

// libc functions that we want to turn into syscalls
INTERPOSE_REMAP(creat64, creat);
INTERPOSE_REMAP(fallocate64, fallocate);
INTERPOSE_REMAP(__fcntl, fcntl);
INTERPOSE_REMAP(fcntl64, fcntl);
INTERPOSE_REMAP(mmap64, mmap);
INTERPOSE_REMAP(open64, open);
