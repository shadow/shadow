#include "shim/shim_shmem.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "main/shmem/shmem_allocator.h"
#include "shim/shim_event.h"

static void _shim_shmemHandleClone(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHD_SHIM_EVENT_CLONE_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(
        &ev->event_data.shmem_blk.serial);

    memcpy(blk.p, (void*)ev->event_data.shmem_blk.plugin_ptr.val,
           ev->event_data.shmem_blk.n);
}

static void _shim_shmemHandleCloneString(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHD_SHIM_EVENT_CLONE_STRING_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(
        &ev->event_data.shmem_blk.serial);

    strncpy(blk.p, (void*)ev->event_data.shmem_blk.plugin_ptr.val,
            ev->event_data.shmem_blk.n);
    // TODO: Shrink buffer to what's actually needed?
}

static void _shim_shmemHandleWrite(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHD_SHIM_EVENT_WRITE_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(
        &ev->event_data.shmem_blk.serial);

    memcpy((void*)ev->event_data.shmem_blk.plugin_ptr.val, blk.p,
           ev->event_data.shmem_blk.n);
}

static void _shim_shmemNotifyComplete(int fd) {
    ShimEvent ev = {
        .event_id = SHD_SHIM_EVENT_SHMEM_COMPLETE,
    };
    shimevent_sendEvent(fd, &ev);
}

void shim_shmemHandleEvent(int fd, const ShimEvent* ev) {
    switch (ev->event_id) {
        case SHD_SHIM_EVENT_CLONE_REQ:
            _shim_shmemHandleClone(ev);
            _shim_shmemNotifyComplete(fd);
            break;
        case SHD_SHIM_EVENT_CLONE_STRING_REQ:
            _shim_shmemHandleCloneString(ev);
            _shim_shmemNotifyComplete(fd);
            break;
        case SHD_SHIM_EVENT_WRITE_REQ:
            _shim_shmemHandleWrite(ev);
            _shim_shmemNotifyComplete(fd);
            break;
        case SHD_SHIM_EVENT_SHMEM_COMPLETE: break;
        default:
            assert(false); // We should never get here!
            break;
    }
}

