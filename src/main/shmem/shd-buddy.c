#include "shd-buddy.h"

#include <stddef.h>

#include "shd-shmem-util.h"

static BuddyControlBlock*
_buddycontrolblock_computeBuddy(BuddyControlBlock* bcb, unsigned order) {
    uintptr_t p = (uintptr_t)bcb;
    p ^= shmem_util_uintPow2k(order);
    return (BuddyControlBlock*)p;
}

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

static void _buddy_listInsert(BuddyControlBlock** list_head,
                              BuddyControlBlock* bcb) {

    // First onto the list.

    if (*list_head == NULL) {
        printf("!\n");
        *list_head = bcb;
        buddycontrolblock_setNxt(bcb, 0);
        buddycontrolblock_setPrv(bcb, 0);
    }
}

static void _buddy_alloc_split_blocks(BuddyControlBlock* bcb, uint32_t k,
                                      uint32_t j, BuddyControlBlock** bcbs) {

    assert(bcb != NULL && bcbs != NULL);

    printf("j=%u\n", j);

    while (j > k) {
        --j;
        BuddyControlBlock* buddy = _buddycontrolblock_computeBuddy(bcb, j);
        buddycontrolblock_setTag(buddy, true);
        buddycontrolblock_setOrder(buddy, j);

        size_t idx = (j - SHD_BUDDY_PART_MIN_ORDER);
        printf("modifying the list at idx %zu\n", idx);

        _buddy_listInsert(&bcbs[idx], buddy);

        for (idx = 0; idx < 4; ++idx) {
            printf("%p ", bcbs[idx]);
        }
        printf("\n");
    }
}

void* buddy_alloc(size_t requested_nbytes, void* meta, void* pool,
                  uint32_t pool_nbytes) {

    BuddyControlBlock** bcbs = meta;
    size_t nbcbs = buddy_metaNumLists(pool_nbytes);

    if (requested_nbytes < 16) {
        requested_nbytes = 16;
    }

    size_t alloc_nbytes = shmem_util_roundUpPow2(requested_nbytes);

    uint32_t k = shmem_util_uintLog2(alloc_nbytes);

    printf("request of size %u\n", k);

    size_t start_idx = k - SHD_BUDDY_PART_MIN_ORDER;
    size_t idx = start_idx;

    BuddyControlBlock** avail = &bcbs[start_idx];

    while (*avail == NULL && idx < nbcbs) {
        avail += 1;
        idx += 1;
    }

    printf("found block of order %zu at %p\n", idx + SHD_BUDDY_PART_MIN_ORDER,
           *avail);

    if (idx == nbcbs) {
        return NULL;
    }

    _buddy_alloc_split_blocks(*avail, k, idx + SHD_BUDDY_PART_MIN_ORDER, bcbs);

    // remove the block
    buddycontrolblock_setTag(*avail, false);
    buddycontrolblock_setOrder(*avail, k);
    bcbs[idx] = buddycontrolblock_nxtBlock(*avail);

    for (idx = 0; idx < 4; ++idx) {
        printf("%p ", bcbs[idx]);
    }
    printf("\n");

    return *avail;
}
