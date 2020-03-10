#include "shd-buddy.h"

#include <stddef.h>

#include "shd-shmem-util.h"

static int buddy_poolMaxOrder(uint32_t pool_nbytes) {
    return shmem_util_uintLog2(pool_nbytes);
}

static size_t buddy_metaNumLists(uint32_t pool_nbytes) {
    return buddy_poolMaxOrder(pool_nbytes) - SHD_BUDDY_PART_MIN_ORDER + 1;
}

uint32_t buddy_goodPoolSizeNBytes(uint32_t requested_nbytes) {
    if (requested_nbytes > SHD_BUDDY_POOL_MAX_NBYTES) {
        return 0;
    }

    if (requested_nbytes < SHD_BUDDY_PART_MIN_NBYTES) {
        return SHD_BUDDY_PART_MIN_NBYTES;
    }

    return shmem_util_roundUpPow2(requested_nbytes);
}

size_t buddy_metaSizeNBytes(uint32_t pool_nbytes) {
    return buddy_metaNumLists(pool_nbytes) * sizeof(void*);
}

void buddy_poolInit(void* pool, size_t pool_nbytes) {
    BuddyControlBlock* bcb = pool;

    buddycontrolblock_setNxt(bcb, 0);
    buddycontrolblock_setPrv(bcb, 0);
    buddycontrolblock_setTag(bcb, true);
    buddycontrolblock_setOrder(bcb, buddy_poolMaxOrder(pool_nbytes));
}

void buddy_metaInit(void* meta, const void* pool, uint32_t pool_nbytes) {
    BuddyControlBlock** bcbs = meta;

    size_t nbcbs = buddy_metaNumLists(pool_nbytes);

    for (size_t idx = 0; idx < nbcbs; ++idx) {
        bcbs[idx] = NULL;
    }

    bcbs[nbcbs - 1] = (BuddyControlBlock*)pool;
}

void* buddy_alloc(size_t requested_nbytes, void* meta, void* pool,
                  uint32_t pool_nbytes) {

    BuddyControlBlock **bcbs = meta;
    size_t nbcbs = buddy_metaNumLists(pool_nbytes);

    if (requested_nbytes < 16) { requested_nbytes = 16; }

    size_t alloc_nbytes  = shmem_util_roundUpPow2(requested_nbytes);

    uint32_t k = shmem_util_uintLog2(alloc_nbytes);
    size_t start_idx = k - SHD_BUDDY_PART_MIN_ORDER;

    BuddyControlBlock **avail = &bcbs[start_idx];

    while (*avail == NULL) {
        avail += 1;
    }

    size_t j = (avail - bcbs) + SHD_BUDDY_PART_MIN_ORDER;

    printf("%u\n", buddycontrolblock_tag(*avail));
    printf("%u\n", buddycontrolblock_order(*avail));

    printf("avail block of order %u\n", j);
    printf("%p\n", *avail);
}
