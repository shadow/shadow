#ifndef SHD_SHIM_SHIM_EVENT_H_
#define SHD_SHIM_SHIM_EVENT_H_

// Communication between Shadow and the shim. This is a header-only library
// used in both places.

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/host/syscall/kernel_types.h"

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
