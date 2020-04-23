#include "shim_event.h"

static inline void shim_determinedSend(int sock_fd, const void* ptr,
                                       size_t nbytes) {
    const char* buf = (const char*)(ptr);
    size_t nbytes_sent = 0;
    ssize_t rc = 0;

    while (nbytes_sent != nbytes) {
        rc = send(sock_fd, buf + nbytes_sent, (nbytes - nbytes_sent), 0);
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
        rc = recv(sock_fd, buf + nbytes_recv, (nbytes - nbytes_recv), 0);
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

void shimevent_recvEvent(int event_fd, ShimEvent* e) {
    shim_determinedRecv(event_fd, e, sizeof(ShimEvent));
}

void shimevent_sendEvent(int event_fd, const ShimEvent* e) {
    shim_determinedSend(event_fd, e, sizeof(ShimEvent));
}
