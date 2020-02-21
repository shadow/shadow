#include <assert.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include "shim.h"
#include "shim_event.h"

int nanosleep(const struct timespec *req, struct timespec *rem) {

    if (req == NULL) {
        errno = EFAULT;
        return -1;
    }

    if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec > 999999999) {
        errno = EINVAL;
        return -1;
    }

    int event_fd = shim_thisThreadEventFD();

    ShimEvent shim_event;
    shim_event.event_id = SHD_SHIM_EVENT_NANO_SLEEP;
    shim_event.event_data.data_nano_sleep.ts = *req;

    SHD_SHIM_LOG("sending event on %d\n", event_fd);
    shimevent_sendEvent(event_fd, &shim_event);

    memset(&shim_event, 0, sizeof(ShimEvent));
    SHD_SHIM_LOG("waiting for event on %d\n", event_fd);
    shimevent_recvEvent(event_fd, &shim_event);
    SHD_SHIM_LOG("got response on %d\n", event_fd);
    assert(shim_event.event_id == SHD_SHIM_EVENT_NANO_SLEEP);

    int rc = (shim_event.event_data.data_nano_sleep.ts.tv_sec == 0 &&
              shim_event.event_data.data_nano_sleep.ts.tv_nsec == 0) ? 0 : -1;

    if (rc == -1) {
        errno = EINTR;
    }

    if (rem != NULL) {
        *rem = shim_event.event_data.data_nano_sleep.ts;
    }

    return rc;
}

unsigned int sleep(unsigned int seconds) {

    struct timespec req, rem;
    memset(&req, 0, sizeof(struct timespec));

    req.tv_sec = seconds;

    nanosleep(&req, &rem);

    return rem.tv_sec;
}
