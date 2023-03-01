#include "shim_shmem.h"

#include <assert.h>
#include <string.h>

#include "lib/shmem/shmem_allocator.h"

#include "lib/shadow-shim-helper-rs/shim_helper.h"

void shim_shmemHandleClone(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHIM_EVENT_ID_CLONE_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(&ev->event_data.shmem_blk.serial);

    memcpy(blk.p, (void*)ev->event_data.shmem_blk.plugin_ptr.val, ev->event_data.shmem_blk.n);
}

void shim_shmemHandleCloneString(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHIM_EVENT_ID_CLONE_STRING_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(&ev->event_data.shmem_blk.serial);

    strncpy(blk.p, (void*)ev->event_data.shmem_blk.plugin_ptr.val, ev->event_data.shmem_blk.n);
    // TODO: Shrink buffer to what's actually needed?
}

void shim_shmemHandleWrite(const ShimEvent* ev) {
    assert(ev && ev->event_id == SHIM_EVENT_ID_WRITE_REQ);

    ShMemBlock blk = shmemserializer_globalBlockDeserialize(&ev->event_data.shmem_blk.serial);

    memcpy((void*)ev->event_data.shmem_blk.plugin_ptr.val, blk.p, ev->event_data.shmem_blk.n);
}

void shim_shmemNotifyComplete(struct IPCData* data) {
    ShimEvent ev = {
        .event_id = SHIM_EVENT_ID_SHMEM_COMPLETE,
    };
    shimevent_sendEventToShadow(data, &ev);
}