#ifndef SHD_SHIM_SHIM_EVENT_H_
#define SHD_SHIM_SHIM_EVENT_H_

// Communication between Shadow and the shim. This is a header-only library
// used in both places.

#include <arpa/inet.h>
#include <stdint.h>
#include <time.h>

#include "../main/host/shd-syscall-types.h"
#include "system-libc.h"

typedef enum {
    SHD_SHIM_EVENT_NULL = 0,
    SHD_SHIM_EVENT_START = 1,
    SHD_SHIM_EVENT_STOP = 2,
    SHD_SHIM_EVENT_SYSCALL = 3,
    SHD_SHIM_EVENT_SYSCALL_COMPLETE = 4
} ShimEventID;

typedef struct _ShimEvent {
    ShimEventID event_id;

    union {
        struct {
            struct timespec ts;
        } data_nano_sleep;
        int rv; // TODO (rwails) hack, remove me
        struct {
            // We wrap this in the surrounding struct in case there's anything
            // else we end up needing in the message besides the literal struct
            // we're going to pass to the syscall handler.
            SysCallArgs syscall_args;
        } syscall;
        struct {
            SysCallReg retval;
        } syscall_complete;
    } event_data;

} ShimEvent;

static inline void shim_determinedSend(int sock_fd, const void* ptr,
                                       size_t nbytes) {
    const char* buf = (const char*)(ptr);
    size_t nbytes_sent = 0;
    ssize_t rc = 0;

    while (nbytes_sent != nbytes) {
        rc = system_libc_send(
            sock_fd, buf + nbytes_sent, (nbytes - nbytes_sent), 0);
        if (rc > -1) {
            nbytes_sent += rc;
        }
    }
}

static inline void shim_determinedRecv(int sock_fd, void* ptr, size_t nbytes) {
    char* buf = (char*)(ptr);
    size_t nbytes_recv = 0;
    ssize_t rc = 0;

    while (nbytes_recv != nbytes) {
        rc = system_libc_recv(
            sock_fd, buf + nbytes_recv, (nbytes - nbytes_recv), 0);
        if (rc > -1) {
            nbytes_recv += rc;
        }
    }
}

static inline void shim_sendUint32_t(int sock_fd, uint32_t value) {
    value = htonl(value);
    shim_determinedSend(sock_fd, &value, sizeof(uint32_t));
}

static inline uint32_t shim_recvUint32_t(int sock_fd) {
    uint32_t value;
    shim_determinedRecv(sock_fd, &value, sizeof(uint32_t));
    return ntohl(value);
}

static inline void shimevent_recvEvent(int event_fd, ShimEvent* e) {
    shim_determinedRecv(event_fd, e, sizeof(ShimEvent));
}

static inline void shimevent_sendEvent(int event_fd, const ShimEvent* e) {
    shim_determinedSend(event_fd, e, sizeof(ShimEvent));
}

#endif // SHD_SHIM_SHIM_EVENT_H_
