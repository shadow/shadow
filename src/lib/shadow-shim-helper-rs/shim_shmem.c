#include "shim_shmem.h"

#include <assert.h>
#include <string.h>

#include "lib/shmem/shmem_allocator.h"

#include "lib/shadow-shim-helper-rs/shim_helper.h"

void shim_shmemHandleClone(const ShimEvent* ev) {
    assert(shimevent_getId(ev) == SHIM_EVENT_CLONE_REQ);
    const ShimEventShmemBlk* shmem_blk = shimevent_getShmemBlkData(ev);
    ShMemBlock blk = shmemserializer_globalBlockDeserialize(&shmem_blk->serial);

    memcpy(blk.p, (void*)shmem_blk->plugin_ptr.val, shmem_blk->n);
}

void shim_shmemHandleCloneString(const ShimEvent* ev) {
    assert(shimevent_getId(ev) == SHIM_EVENT_CLONE_STRING_REQ);
    const ShimEventShmemBlk* shmem_blk = shimevent_getShmemBlkData(ev);
    ShMemBlock blk = shmemserializer_globalBlockDeserialize(&shmem_blk->serial);

    strncpy(blk.p, (void*)shmem_blk->plugin_ptr.val, shmem_blk->n);
    // TODO: Shrink buffer to what's actually needed?
}

void shim_shmemHandleWrite(const ShimEvent* ev) {
    assert(shimevent_getId(ev) == SHIM_EVENT_WRITE_REQ);
    const ShimEventShmemBlk* shmem_blk = shimevent_getShmemBlkData(ev);
    ShMemBlock blk = shmemserializer_globalBlockDeserialize(&shmem_blk->serial);

    memcpy((void*)shmem_blk->plugin_ptr.val, blk.p, shmem_blk->n);
}

void shim_shmemNotifyComplete(struct IPCData* data) {
    ShimEvent ev;
    shimevent_initShmemComplete(&ev);
    shimevent_sendEventToShadow(data, &ev);
}