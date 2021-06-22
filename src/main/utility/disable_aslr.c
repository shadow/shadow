#include "disable_aslr.h"

#include <errno.h>
#include <string.h>

#include <sys/personality.h>

#include "lib/logger/logger.h"

void disable_aslr() {
    int prev_persona = personality(ADDR_NO_RANDOMIZE);

    if (prev_persona != -1) {
        info("ASLR disabled for processes forked from this parent process.");
    } else {
        const char *err = strerror(errno);
        warning("Could not disable plugin address space layout randomization: %s",
                err);
    }
}
