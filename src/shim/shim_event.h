#ifndef SHD_SHIM_SHIM_EVENT_H_
#define SHD_SHIM_SHIM_EVENT_H_
#include <stdint.h>
#include <time.h>

#define SHD_SHIM_EVENT_START 0
#define SHD_SHIM_EVENT_NANO_SLEEP 1

typedef uint32_t ShimEventID;

typedef struct _ShimEventDataNanoSleep {
    struct timespec ts;
} ShimEventDataNanoSleep;

typedef struct _ShimEvent {
    ShimEventID event_id;

    union _EventData {
        ShimEventDataNanoSleep data_nano_sleep;
    } event_data;

} ShimEvent;

void shimevent_recvEvent(int event_fd, ShimEvent *e);
void shimevent_sendEvent(int event_fd, const ShimEvent *e);

#endif // SHD_SHIM_SHIM_EVENT_H_
