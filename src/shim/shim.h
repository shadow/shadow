#ifndef SHD_SHIM_SHIM_H_
#define SHD_SHIM_SHIM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

FILE *shim_logFD();
int shim_thisThreadEventFD();

#define SHD_SHIM_LOG(...) \
    fprintf(shim_logFD(), "[shd-shim]\t"); \
    fprintf(shim_logFD(), __VA_ARGS__)

// Disables syscall interposition for the current thread if it's enabled. (And
// if not, increments a counter). Should be matched with a call to
// _shim_enable_interposition.
//
// Every call to this function should have corresponding call(s) to
// _shim_return.
void shim_disableInterposition();

// Re-enables syscall interposition for the current thread.
void shim_enableInterposition();

// Whether syscall interposition is currently enabled.
bool shim_interpositionEnabled();

// Used in the constructor attribute to initialize the shim.
#define SHIM_CONSTRUCTOR_PRIORITY 200

#endif // SHD_SHIM_SHIM_H_
