#include "main/shmem/buddy.h"

#include <stddef.h>

#include "main/shmem/shmem_util.h"

static BuddyControlBlock*
_buddycontrolblock_computeBuddy(BuddyControlBlock* bcb, unsigned order,
                                void* pool) {

    const uint8_t* bcb_p = (const uint8_t*)bcb;
    const uint8_t* pool_p = (const uint8_t*)pool;

    size_t nbytes = bcb_p - pool_p;

    uintptr_t p = (uintptr_t)(bcb_p - pool_p);
    p ^= shmem_util_uintPow2k(order);
    return (BuddyControlBlock*)(p + pool);
}

static unsigned buddy_poolMaxOrder(uint32_t pool_nbytes) {
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
        *list_head = bcb;
        buddycontrolblock_setNxt(bcb, 0);
        buddycontrolblock_setPrv(bcb, 0);
    } else { // linear scan to find list entry position

        BuddyControlBlock* prv = list_head[0];
        BuddyControlBlock* nxt = buddycontrolblock_nxtBlock(prv);

        if (bcb < prv) { // bcb is the new list head
            buddycontrolblock_setPrv(bcb, 0);
            list_head[0] = bcb;
            buddycontrolblock_setNxtBlock(bcb, prv);
            buddycontrolblock_setPrvBlock(prv, bcb);
        } else { // it goes in the middle of the list

            while (nxt != NULL && nxt < bcb) {
                prv = buddycontrolblock_nxtBlock(prv);
                nxt = buddycontrolblock_nxtBlock(nxt);
            }

            assert(prv != NULL && prv < bcb);
            assert(nxt == NULL || bcb < nxt);

            buddycontrolblock_setNxtBlock(prv, bcb);

            buddycontrolblock_setPrvBlock(bcb, prv);
            buddycontrolblock_setNxtBlock(bcb, nxt);

            if (nxt != NULL) {
                buddycontrolblock_setPrvBlock(nxt, bcb);
            }
        }
    }
}

static void _buddy_listRemove(BuddyControlBlock** list_head,
                              BuddyControlBlock* bcb) {

    unsigned order = buddycontrolblock_order(bcb);
    BuddyControlBlock* nxt = buddycontrolblock_nxtBlock(bcb);

    if (list_head[0] == bcb) { // handle the head node

        list_head[0] = nxt;

        if (nxt != NULL) {
            assert(order == buddycontrolblock_order(nxt));
            assert(buddycontrolblock_tag(nxt));
            buddycontrolblock_setPrvBlock(nxt, 0);
        }

    } else {
        // we aren't the head, so our prv should not be null.
        BuddyControlBlock* prv = buddycontrolblock_prvBlock(bcb);
        assert(prv != NULL);

        assert(order == buddycontrolblock_order(prv));
        assert(buddycontrolblock_tag(prv));

        buddycontrolblock_setNxtBlock(prv, nxt);

        if (nxt != NULL) {
            assert(order == buddycontrolblock_order(nxt));
            assert(buddycontrolblock_tag(nxt));
            buddycontrolblock_setPrvBlock(nxt, prv);
        }
    }
}

static void _buddy_alloc_split_blocks(BuddyControlBlock* bcb, uint32_t k,
                                      uint32_t j, BuddyControlBlock** bcbs) {

    assert(bcb != NULL && bcbs != NULL);

    while (j > k) {
        --j;
        BuddyControlBlock* split =
            (BuddyControlBlock*)((uint8_t*)bcb + shmem_util_uintPow2k(j));
        buddycontrolblock_setTag(split, true);
        buddycontrolblock_setOrder(split, j);

        size_t idx = (j - SHD_BUDDY_PART_MIN_ORDER);

        _buddy_listInsert(&bcbs[idx], split);
    }
}

#ifndef NDEBUG
static void _print(void* pool, size_t pool_nbytes, void* meta) {

    fprintf(stderr, "-----------------------\n");

    BuddyControlBlock** bcbs = meta;
    unsigned max_order = buddy_poolMaxOrder(pool_nbytes);

    for (size_t idx = 0; idx + SHD_BUDDY_PART_MIN_ORDER < max_order + 1;
         ++idx) {

        fprintf(stderr, "[%zu] ", idx + SHD_BUDDY_PART_MIN_ORDER);

        BuddyControlBlock* p = bcbs[idx];

        do {

            if (p != NULL) {
                const char* format = "(%u <- (A: %d, S: %u, T: %u) -> %u) | ";
                BuddyControlBlock* prv_block = buddycontrolblock_prvBlock(p);
                BuddyControlBlock* nxt_block = buddycontrolblock_nxtBlock(p);

                unsigned prv =
                    prv_block == NULL ? 0 : (uint8_t*)p - (uint8_t*)prv_block;
                unsigned nxt =
                    nxt_block == NULL ? 0 : (uint8_t*)nxt_block - (uint8_t*)p;

                unsigned addr = (uint8_t*)p - (uint8_t*)pool;

                unsigned sz = shmem_util_uintPow2k(buddycontrolblock_order(p));
                bool tag = buddycontrolblock_tag(p);

                fprintf(stderr, format, prv, addr, sz, tag, nxt);

                p = nxt_block;
            } else {
                fprintf(stderr, "<NIL>");
            }

        } while (p != NULL);

        fprintf(stderr, "\n");
    }

    fflush(stderr);
}
#endif // NDEBUG

void* buddy_alloc(size_t requested_nbytes, void* meta, void* pool,
                  uint32_t pool_nbytes) {

    if (requested_nbytes == 0) {
        return NULL;
    }

    BuddyControlBlock** bcbs = meta;
    size_t nbcbs = buddy_metaNumLists(pool_nbytes);

    size_t alloc_nbytes =
        shmem_util_roundUpPow2(requested_nbytes + sizeof(BuddyControlBlock));

    uint32_t k = shmem_util_uintLog2(alloc_nbytes);

    size_t start_idx = k - SHD_BUDDY_PART_MIN_ORDER;
    size_t idx = start_idx;

    BuddyControlBlock** avail = &bcbs[start_idx];

    while (idx < nbcbs && *avail == NULL) {
        avail += 1;
        idx += 1;
    }

    if (idx == nbcbs) {
        return NULL;
    }

    BuddyControlBlock* ret = *avail;
    _buddy_alloc_split_blocks(*avail, k, idx + SHD_BUDDY_PART_MIN_ORDER, bcbs);

    // remove the block
    _buddy_listRemove(&bcbs[idx], ret);
    buddycontrolblock_setTag(ret, false);
    buddycontrolblock_setOrder(ret, k);

    return ((uint8_t*)ret + sizeof(BuddyControlBlock));
}

static bool _buddycontrolblock_buddy_available(BuddyControlBlock* buddy,
                                               unsigned order) {
    unsigned buddy_order = buddycontrolblock_order(buddy);
    bool tag = buddycontrolblock_tag(buddy);

    if (!tag || buddy_order != order) {
        return false;
    }

    return true;
}

void buddy_free(void* p, void* meta, void* pool, size_t pool_nbytes) {
    BuddyControlBlock** bcbs = meta;

    if (p == NULL) {
        return;
    }

    BuddyControlBlock* bcb = buddy_retreiveBCB(p);

    unsigned bcb_order = buddycontrolblock_order(bcb);
    unsigned max_order = buddy_poolMaxOrder(pool_nbytes);

    BuddyControlBlock* buddy =
        _buddycontrolblock_computeBuddy(bcb, bcb_order, pool);
    size_t idx = 0;

    while (bcb_order < max_order &&
           _buddycontrolblock_buddy_available(buddy, bcb_order)) {
        // remove the buddy block from his list
        idx = bcb_order - SHD_BUDDY_PART_MIN_ORDER;
        _buddy_listRemove(&bcbs[idx], buddy);
        bcb = bcb < buddy ? bcb : buddy;

        bcb_order += 1;
        buddycontrolblock_setOrder(bcb, bcb_order);
        buddy = _buddycontrolblock_computeBuddy(bcb, bcb_order, pool);
    }

    buddycontrolblock_setTag(bcb, true);
    idx = bcb_order - SHD_BUDDY_PART_MIN_ORDER;

    _buddy_listInsert(&bcbs[idx], bcb);

}
