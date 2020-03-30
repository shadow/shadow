// Defines higher-level functions C library functions: those that are
// documented in man section 3. (See `man man`).

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "shim-event.h"
#include "shim.h"

// man 3 usleep
int usleep(useconds_t usec) {
    struct timespec req, rem;
    req.tv_sec = usec / 1000000;
    const long remaining_usec = usec - req.tv_sec * 1000000;
    req.tv_nsec = remaining_usec * 1000;

    return nanosleep(&req, &rem);
}

// man 3 sleep
unsigned int sleep(unsigned int seconds) {
    struct timespec req = {.tv_sec = seconds};
    struct timespec rem = { 0 };

    if (nanosleep(&req, &rem) == 0) {
        return 0;
    }

    return rem.tv_sec;
}

