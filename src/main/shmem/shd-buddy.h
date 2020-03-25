#ifndef SHD_BUDDY_H_
#define SHD_BUDDY_H_

/* Intended to be private to shd-shmem-allocator. */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHD_BUDDY_ORDER_BITS 5
#define SHD_BUDDY_ORDER_MASK 134217727 // 2^(32 - 5) - 1
#define SHD_BUDDY_TAG_BITS 1
#define SHD_BUDDY_TAG_MASK 2147483647 // 2^(32 - 1) - 1

#define SHD_BUDDY_POOL_MAX_NBYTES 134217728 // 2^(32 - 5)

#define SHD_BUDDY_PART_MIN_NBYTES 16 // 8 for control block, 8 for data
#define SHD_BUDDY_PART_MIN_ORDER 4
#define SHD_BUDDY_PART_MAX_ORDER 27

#define SHD_BUDDY_META_MAX_NBYTES                                              \
    (sizeof(void*) * (SHD_BUDDY_PART_MAX_ORDER - SHD_BUDDY_PART_MIN_ORDER + 1))

typedef struct _BuddyControlBlock {
    uint32_t _nxt;
    uint32_t _prv;

    // The nxt and prv links are going to be packed with the order of the block
    // and the avail tag.  Don't access directly!
} BuddyControlBlock;

// rwails: shouldn't be true for every sane compiler
_Static_assert(sizeof(BuddyControlBlock) == 8,
               "BuddyControlBlock padded to incorrect length by compiler");

static inline unsigned buddycontrolblock_order(const BuddyControlBlock* bcb) {
    assert(bcb != NULL);
    return bcb->_nxt >> (32 - SHD_BUDDY_ORDER_BITS);
}

static inline void buddycontrolblock_setOrder(BuddyControlBlock* bcb,
                                              unsigned value) {
    assert(bcb != NULL && value < 32); // 0 <= order <= 31
    bcb->_nxt &= SHD_BUDDY_ORDER_MASK;
    bcb->_nxt |= (value << (32 - SHD_BUDDY_ORDER_BITS));
}

static inline uint32_t buddycontrolblock_nxt(const BuddyControlBlock* bcb) {
    assert(bcb != NULL);
    return bcb->_nxt & SHD_BUDDY_ORDER_MASK;
}

static inline void buddycontrolblock_setNxt(BuddyControlBlock* bcb,
                                            unsigned value) {
    assert(value <= SHD_BUDDY_ORDER_MASK);
    bcb->_nxt &= ~SHD_BUDDY_ORDER_MASK;
    bcb->_nxt |= value;
}

static inline bool buddycontrolblock_tag(const BuddyControlBlock* bcb) {
    assert(bcb != NULL);
    return bcb->_prv >> (32 - SHD_BUDDY_TAG_BITS);
}

static inline void buddycontrolblock_setTag(BuddyControlBlock* bcb,
                                            bool value) {
    assert(bcb != NULL);
    bcb->_prv &= SHD_BUDDY_TAG_MASK;
    bcb->_prv |= (value << (32 - SHD_BUDDY_TAG_BITS));
}

static inline uint32_t buddycontrolblock_prv(const BuddyControlBlock* bcb) {
    assert(bcb != NULL);
    return bcb->_prv & SHD_BUDDY_TAG_MASK;
}

static inline void buddycontrolblock_setPrv(BuddyControlBlock* bcb,
                                            unsigned value) {
    assert(value <= SHD_BUDDY_TAG_MASK);
    bcb->_prv &= ~SHD_BUDDY_TAG_MASK;
    bcb->_prv |= value;
}

static inline BuddyControlBlock*
buddycontrolblock_nxtBlock(const BuddyControlBlock* bcb) {
    assert(bcb != NULL);
    uint32_t nxt = buddycontrolblock_nxt(bcb);
    if (nxt == 0) {
        return NULL;
    } else {
        uint8_t* p = (uint8_t*)bcb;
        return (BuddyControlBlock*)(p + nxt);
    }
}

static inline void buddycontrolblock_setNxtBlock(BuddyControlBlock* bcb,
                                                 const BuddyControlBlock* nxt) {
    assert(bcb != NULL);

    unsigned offset = 0;

    if (nxt != NULL) {
        uint8_t *p = (uint8_t*)bcb, *q = (uint8_t*)nxt;
        assert(p <= q);
        offset = (q - p);
    }

    buddycontrolblock_setNxt(bcb, offset);
}

static inline BuddyControlBlock*
buddycontrolblock_prvBlock(const BuddyControlBlock* bcb) {
    assert(bcb != NULL);
    uint32_t prv = buddycontrolblock_prv(bcb);
    if (prv == 0) {
        return NULL;
    } else {
        uint8_t* p = (uint8_t*)bcb;
        return (BuddyControlBlock*)(p - prv);
    }
}

static inline void buddycontrolblock_setPrvBlock(BuddyControlBlock* bcb,
                                                 const BuddyControlBlock* prv) {
    assert(bcb != NULL);

    unsigned offset = 0;

    if (prv != NULL) {
        uint8_t *p = (uint8_t*)bcb, *q = (uint8_t*)prv;
        assert(q <= p);
        offset = (p - q);
    }

    buddycontrolblock_setPrv(bcb, offset);
}

uint32_t buddy_goodPoolSizeNBytes(uint32_t requested_nbytes);

size_t buddy_metaSizeNBytes(uint32_t pool_nbytes);

void buddy_poolInit(void* pool, size_t pool_nbytes);
void buddy_metaInit(void* meta, const void* pool, uint32_t pool_nbytes);

void* buddy_alloc(size_t requested_nbytes, void* meta, void* pool,
                  uint32_t pool_nbytes);

void buddy_free(void* p, void* meta, void* pool, size_t pool_nbytes);

#endif // SHD_BUDDY_H_
