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

// FIXME: Ideally we split or make 2 version of the shim and load the
// appropriate version based on whether we're using the shim-pipe.  In the
// meantime we rely on run-time checks.
bool shim_usingInterposePreload();

#endif // SHD_SHIM_SHIM_H_
