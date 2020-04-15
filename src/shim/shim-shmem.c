#include "shim-shmem.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "../main/shmem/shd-shmem-allocator.h"
#include "shim-event.h"

static void _shim_shmemHandleClone(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHD_SHIM_EVENT_CLONE_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(
        &ev->event_data.shmem_blk.serial);

    memcpy(blk.p, (void*)ev->event_data.shmem_blk.plugin_ptr.val,
           ev->event_data.shmem_blk.n);
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

static void _shim_shmemHandleEvent(int fd, const ShimEvent* ev) {
    switch (ev->event_id) {
        case SHD_SHIM_EVENT_CLONE_REQ:
            _shim_shmemHandleClone(ev);
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

void shim_shmemLoop(int fd) {

    ShimEvent ev = {0};

    do {
        shimevent_recvEvent(fd, &ev);
        _shim_shmemHandleEvent(fd, &ev);
    } while (ev.event_id != SHD_SHIM_EVENT_SHMEM_COMPLETE);
}
