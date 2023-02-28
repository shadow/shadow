#ifndef SHD_SHIM_SHIM_EVENT_H_
#define SHD_SHIM_SHIM_EVENT_H_

// Communication between Shadow and the shim. This is a header-only library
// used in both places.

#include "lib/shadow-shim-helper-rs/shim_helper.h"
#include "lib/shmem/shmem_allocator.h"
#include "main/host/syscall/kernel_types.h"

typedef enum {
    // Next val: 13
    SHIM_EVENT_ID_NULL = 0,
    SHIM_EVENT_ID_START = 1,
    // The whole process has died.
    // We inject this event to trigger cleanup after we've detected that the
    // native process has died.
    SHIM_EVENT_ID_PROCESS_DEATH = 2,
    SHIM_EVENT_ID_SYSCALL = 3,
    SHIM_EVENT_ID_SYSCALL_COMPLETE = 4,
    SHIM_EVENT_ID_SYSCALL_DO_NATIVE = 8,
    SHIM_EVENT_ID_CLONE_REQ = 5,
    SHIM_EVENT_ID_CLONE_STRING_REQ = 9,
    SHIM_EVENT_ID_SHMEM_COMPLETE = 6,
    SHIM_EVENT_ID_WRITE_REQ = 7,
    SHIM_EVENT_ID_BLOCK = 10,
    SHIM_EVENT_ID_ADD_THREAD_REQ = 11,
    SHIM_EVENT_ID_ADD_THREAD_PARENT_RES = 12,
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
