#ifndef SHD_SHIM_SHIM_EVENT_H_
#define SHD_SHIM_SHIM_EVENT_H_

// Communication between Shadow and the shim. This is a header-only library
// used in both places.

#include <arpa/inet.h>
#include <stdint.h>
#include <time.h>

#include "main/host/syscall_types.h"
#include "main/shmem/shmem_allocator.h"

typedef enum {
    // Next val: 9
    SHD_SHIM_EVENT_NULL = 0,
    SHD_SHIM_EVENT_START = 1,
    SHD_SHIM_EVENT_STOP = 2,
    SHD_SHIM_EVENT_SYSCALL = 3,
    SHD_SHIM_EVENT_SYSCALL_COMPLETE = 4,
    SHD_SHIM_EVENT_SYSCALL_DO_NATIVE = 8,
    SHD_SHIM_EVENT_CLONE_REQ = 5,
    SHD_SHIM_EVENT_SHMEM_COMPLETE = 6,
    SHD_SHIM_EVENT_WRITE_REQ = 7
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

        struct {
            ShMemBlockSerialized serial;
            PluginPtr plugin_ptr;
            size_t n;
        } shmem_blk;

    } event_data;

} ShimEvent;

void shimevent_recvEvent(int event_fd, ShimEvent* e);
void shimevent_sendEvent(int event_fd, const ShimEvent* e);

#endif // SHD_SHIM_SHIM_EVENT_H_
