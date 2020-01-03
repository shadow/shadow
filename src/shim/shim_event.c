#include "shim_event.h"

#include <stdint.h>

#include "shim.h"

void shimevent_recvEvent(int event_fd, ShimEvent *e) {
    shim_determinedRecv(event_fd, e, sizeof(ShimEvent));
}

void shimevent_sendEvent(int event_fd, const ShimEvent *e) {
    shim_determinedSend(event_fd, e, sizeof(ShimEvent));
}
