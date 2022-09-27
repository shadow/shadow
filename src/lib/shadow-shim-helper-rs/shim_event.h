#ifndef SHD_SHIM_SHIM_EVENT_H_
#define SHD_SHIM_SHIM_EVENT_H_

// Communication between Shadow and the shim. This is a header-only library
// used in both places.

#include "lib/shmem/shmem_allocator.h"
#include "main/host/syscall/kernel_types.h"
#include "main/host/syscall_types.h"

typedef enum {
    // Next val: 13
    SHD_SHIM_EVENT_NULL = 0,
    SHD_SHIM_EVENT_START = 1,
    SHD_SHIM_EVENT_STOP = 2,
    SHD_SHIM_EVENT_SYSCALL = 3,
    SHD_SHIM_EVENT_SYSCALL_COMPLETE = 4,
    SHD_SHIM_EVENT_SYSCALL_DO_NATIVE = 8,
    SHD_SHIM_EVENT_CLONE_REQ = 5,
    SHD_SHIM_EVENT_CLONE_STRING_REQ = 9,
    SHD_SHIM_EVENT_SHMEM_COMPLETE = 6,
    SHD_SHIM_EVENT_WRITE_REQ = 7,
    SHD_SHIM_EVENT_BLOCK = 10,
    SHD_SHIM_EVENT_ADD_THREAD_REQ = 11,
    SHD_SHIM_EVENT_ADD_THREAD_PARENT_RES = 12,
} ShimEventID;

typedef struct _ShimEvent {
    ShimEventID event_id;

    union {
        struct {
            // We wrap this in the surrounding struct in case there's anything
            // else we end up needing in the message besides the literal struct
            // we're going to pass to the syscall handler.
            SysCallArgs syscall_args;
        } syscall;

        struct {
            SysCallReg retval;
            // Whether the syscall is eligible to be restarted. Only applicable
            // when retval is -EINTR. See signal(7).
            bool restartable;
        } syscall_complete;

        struct {
            ShMemBlockSerialized serial;
            PluginPtr plugin_ptr;
            size_t n;
        } shmem_blk;

        struct {
            ShMemBlockSerialized ipc_block;
        } add_thread_req;
    } event_data;

} ShimEvent;

#endif // SHD_SHIM_SHIM_EVENT_H_
