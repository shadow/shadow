#ifndef SHD_SHIM_SHIM_EVENT_H_
#define SHD_SHIM_SHIM_EVENT_H_
#include <stdint.h>
#include <time.h>

#define SHD_SHIM_EVENT_NULL 0
#define SHD_SHIM_EVENT_START 1
#define SHD_SHIM_EVENT_STOP 2
#define SHD_SHIM_EVENT_NANO_SLEEP 3

// TODO (rwails) hack, change me
#define SHD_SHIM_EVENT_NANO_SLEEP_COMPLETE 4

typedef uint32_t ShimEventID;

typedef struct _ShimEventDataNanoSleep {
    struct timespec ts;
} ShimEventDataNanoSleep;

typedef struct _ShimEvent {
    ShimEventID event_id;

    union _EventData {
        ShimEventDataNanoSleep data_nano_sleep;
        int rv; // TODO (rwails) hack, remove me
    } event_data;

} ShimEvent;

void shimevent_recvEvent(int event_fd, ShimEvent *e);
void shimevent_sendEvent(int event_fd, const ShimEvent *e);

#endif // SHD_SHIM_SHIM_EVENT_H_
