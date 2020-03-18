#include "shd-buddy.h"
#include "shd-shmem-allocator.h"
#include "shd-shmem-file.h"
#include "shd-shmem-util.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPECT_TRUE(...) (rc |= (1 - (__VA_ARGS__)))

static int buddycontrolblock_testOrder() {
    int rc = 0;

    BuddyControlBlock bcb;
    memset(&bcb, 0, sizeof(BuddyControlBlock));

    EXPECT_TRUE(buddycontrolblock_order(&bcb) == 0);

    for (unsigned idx = 0; idx < 32; ++idx) {
        buddycontrolblock_setOrder(&bcb, idx);
        EXPECT_TRUE(buddycontrolblock_order(&bcb) == idx);
    }

    if (rc) {
        fprintf(stderr, "failed buddycontrolblock_testOrder\n");
    }
    return rc;
}

static int buddycontrolblock_testOrderAndNxt() {
    enum { kNTests = 1000 };

    int rc = 0;

    BuddyControlBlock bcb;
    memset(&bcb, 0, sizeof(BuddyControlBlock));

    uint32_t* nxt_values = calloc(kNTests, 4);

    for (size_t idx = 0; idx < kNTests; ++idx) {
        nxt_values[idx] = rand() % (SHD_BUDDY_ORDER_MASK + 1);
    }
    nxt_values[0] = 0;
    nxt_values[1] = SHD_BUDDY_ORDER_MASK;

    for (unsigned idx = 0; idx < 32; ++idx) {
        buddycontrolblock_setOrder(&bcb, idx);
        for (unsigned jdx = 0; jdx < kNTests; ++jdx) {

            uint32_t nxt_value = nxt_values[jdx];

            buddycontrolblock_setNxt(&bcb, nxt_value);
            EXPECT_TRUE(buddycontrolblock_order(&bcb) == idx);
            EXPECT_TRUE(buddycontrolblock_nxt(&bcb) == nxt_value);
        }
    }

    for (unsigned idx = 0; idx < kNTests; ++idx) {
        uint32_t nxt_value = nxt_values[idx];
        buddycontrolblock_setNxt(&bcb, nxt_value);
        for (unsigned jdx = 0; jdx < 32; ++jdx) {
            buddycontrolblock_setOrder(&bcb, jdx);
            EXPECT_TRUE(buddycontrolblock_order(&bcb) == jdx);
            EXPECT_TRUE(buddycontrolblock_nxt(&bcb) == nxt_value);
        }
    }

    free(nxt_values);

    if (rc) {
        fprintf(stderr, "failed buddycontrolblock_testOrderAndNxt\n");
    }
    return rc;
}

static int buddycontrolblock_testTagAndPrv() {
    enum { kNTests = 1000 };

    int rc = 0;

    BuddyControlBlock bcb;
    memset(&bcb, 0, sizeof(BuddyControlBlock));

    uint32_t* prv_values = calloc(kNTests, 4);

    for (size_t idx = 0; idx < kNTests; ++idx) {
        prv_values[idx] = rand() % ((uint32_t)SHD_BUDDY_TAG_MASK + 1);
    }
    prv_values[0] = 0;
    prv_values[1] = SHD_BUDDY_TAG_MASK;

    for (unsigned idx = 0; idx < 2; ++idx) {
        buddycontrolblock_setTag(&bcb, idx);
        for (unsigned jdx = 0; jdx < kNTests; ++jdx) {

            uint32_t prv_value = prv_values[jdx];

            buddycontrolblock_setPrv(&bcb, prv_value);
            EXPECT_TRUE(buddycontrolblock_tag(&bcb) == idx);
            EXPECT_TRUE(buddycontrolblock_prv(&bcb) == prv_value);
        }
    }

    for (unsigned idx = 0; idx < kNTests; ++idx) {
        uint32_t prv_value = prv_values[idx];
        buddycontrolblock_setPrv(&bcb, prv_value);
        for (unsigned jdx = 0; jdx < 2; ++jdx) {
            buddycontrolblock_setTag(&bcb, jdx);
            EXPECT_TRUE(buddycontrolblock_tag(&bcb) == jdx);
            EXPECT_TRUE(buddycontrolblock_prv(&bcb) == prv_value);
        }
    }

    free(prv_values);

    if (rc) {
        fprintf(stderr, "failed buddycontrolblock_testTagAndPrv\n");
    }
    return rc;
}

static int buddycontrolblock_testGoodSizes() {

    int rc = 0;

    EXPECT_TRUE(buddy_goodPoolSizeNBytes(1) == 16);
    EXPECT_TRUE(buddy_goodPoolSizeNBytes(33) == 64);
    EXPECT_TRUE(buddy_goodPoolSizeNBytes(32) == 32);
    EXPECT_TRUE(buddy_goodPoolSizeNBytes(INT32_MAX) == 0);

    if (rc) {
        fprintf(stderr, "failed buddycontrolblock_testGoodSizes\n");
    }
    return rc;
}

static int buddy_test(size_t pool_nbytes) {

    int rc = 0;

    static const size_t nallocs = 1000;

    struct Alloc {
        size_t nbytes;
        void *bud;
        void *mal;
    };

    struct Alloc allocs[nallocs];

    unsigned max_alloc = shmem_util_uintLog2(pool_nbytes);
    size_t n = (max_alloc - SHD_BUDDY_PART_MIN_ORDER + 1);

    size_t meta_nbytes = buddy_metaSizeNBytes(pool_nbytes);

    void *pool = calloc(1, pool_nbytes);
    void *meta = calloc(1, meta_nbytes);

    buddy_poolInit(pool, pool_nbytes);
    buddy_metaInit(meta, pool, pool_nbytes);

    for (size_t idx = 0; idx < nallocs; ++idx) {
        unsigned alloc_order = SHD_BUDDY_PART_MIN_ORDER + (rand() % n);
        size_t alloc_nbytes = shmem_util_uintPow2k(alloc_order) - 8;

        void *p = buddy_alloc(alloc_nbytes, meta, pool, pool_nbytes);

        if (p != NULL) {
            void *q = malloc(alloc_nbytes);
            assert(q != NULL);
            *(uint32_t*)p = rand();
            *(uint32_t*)q = *(uint32_t*)p;

            struct Alloc a = {.nbytes = alloc_nbytes, .bud = p, .mal = q};
            allocs[idx] = a;
        } else {
            struct Alloc a = {.nbytes = 0, .bud = NULL, .mal = NULL};
            allocs[idx] = a;
        }
    }

    for (size_t idx = 0; idx < nallocs; ++idx) {
        if (allocs[idx].bud != NULL) {

            uint32_t *p = (uint32_t *)allocs[idx].bud;
            uint32_t *q = (uint32_t *)allocs[idx].mal;
            EXPECT_TRUE(*p == *q);

            buddy_free(allocs[idx].bud, meta, pool, pool_nbytes);
            free(allocs[idx].mal);
        }
    }

    free(pool);
    free(meta);

    if (rc) {
        fprintf(stderr, "failed buddy_test\n");
    }
    return rc;
}

static int shmemfile_testGoodAlloc(size_t requested_nbytes) {

    size_t good_nbytes = shmemfile_goodSizeNBytes(requested_nbytes);

    ShMemFile shmf;
    int rc = shmemfile_alloc(good_nbytes, &shmf);
    if (rc == 0) {
        rc = shmemfile_free(&shmf);
    }

    if (rc) {
        fprintf(stderr, "failed shmemfile_testGoodAlloc\n");
    }
    return rc;
}

static int util_testLog2() {
    int rc = 0;

    for (uint32_t idx = 1; idx < 32000; ++idx) {
        uint32_t lhs = log2(idx);
        uint32_t rhs = shmem_util_uintLog2(idx);
        EXPECT_TRUE(lhs == rhs);
    }

    if (rc) {
        fprintf(stderr, "failed util_testLog2\n");
    }

    return rc;
}

static int util_testPow2k() {
    int rc = 0;

    EXPECT_TRUE(shmem_util_uintPow2k(0) == 1);
    EXPECT_TRUE(shmem_util_uintPow2k(1) == 2);
    EXPECT_TRUE(shmem_util_uintPow2k(2) == 4);
    EXPECT_TRUE(shmem_util_uintPow2k(31) == 2147483648);

    if (rc) {
        fprintf(stderr, "failed util_testPow2k\n");
    }

    return rc;
}

static int shmemallocator_test() {
    int rc = 0;

    ShMemAllocator *allocator = shmemallocator_create();

    ShMemBlock blk1 = shmemallocator_alloc(allocator, 134217728 - 100);

    shmemallocator_free(allocator, &blk1);

    blk1 = shmemallocator_alloc(allocator, 134217728 - 100);
    ShMemBlock blk2 = shmemallocator_alloc(allocator, 134217728 + 1);

    shmemallocator_free(allocator, &blk1);
    shmemallocator_free(allocator, &blk2);

    blk1 = shmemallocator_alloc(allocator, 134217728 - 100);
    blk2 = shmemallocator_alloc(allocator, 134217728 + 1);

    shmemallocator_free(allocator, &blk1);
    shmemallocator_free(allocator, &blk2);

    ShMemBlock blk3 = shmemallocator_alloc(allocator, 2040);
    ShMemBlock blk4 = shmemallocator_alloc(allocator, 2040);
    shmemallocator_free(allocator, &blk3);
    ShMemBlock blk5 = shmemallocator_alloc(allocator, 2040);
    shmemallocator_free(allocator, &blk4);
    ShMemBlock blk6 = shmemallocator_alloc(allocator, 8192);

    memcpy(blk6.p, "hello", 6);
    printf("1) %p %s\n", blk6.p, blk6.p);

    ShMemBlockSerialized serial5 = shmemallocator_blockSerialize(allocator, &blk5);
    ShMemBlockSerialized serial6 = shmemallocator_blockSerialize(allocator, &blk6);

    printf("%s %zu %zu\n", serial6.name, serial6.nbytes, serial6.offset);

    // ShMemBlock d1 = shmemallocator_blockDeserialize(allocator, &serial5);
    ShMemBlock d2 = shmemallocator_blockDeserialize(allocator, &serial6);
    printf("2) %p %s\n", d2.p, d2.p);

    ShMemSerializer *serializer = shmemserializer_create();

    ShMemBlock d3 = shmemserializer_blockDeserialize(serializer, &serial5);
    ShMemBlock d4 = shmemserializer_blockDeserialize(serializer, &serial6);
    ShMemBlock d5 = shmemserializer_blockDeserialize(serializer, &serial5);
    ShMemBlock d6 = shmemserializer_blockDeserialize(serializer, &serial6);

    printf("%p %s\n", d6.p, d6.p);

    // shmemallocator_free(allocator, &blk6);

    shmemallocator_destroy(allocator);
    shmemserializer_destroy(serializer);

    if (rc) {
        fprintf(stderr, "failed shmemallocator_test\n");
    }

    return rc;
}

int main(int argc, char** argv) {

    int rc = 0;

    rc |= shmemallocator_test();
    return rc;

    /* buddy */
    rc |= buddycontrolblock_testOrder();
    rc |= buddycontrolblock_testOrderAndNxt();
    rc |= buddycontrolblock_testTagAndPrv();
    rc |= buddycontrolblock_testGoodSizes();

    rc |= buddy_test(4096);

    for (size_t idx = 0; idx < 100; ++idx) {
        rc |= buddy_test(SHD_BUDDY_POOL_MAX_NBYTES);
    }

    /* shmemfile */
    rc |= shmemfile_testGoodAlloc(100);
    rc |= shmemfile_testGoodAlloc(1000);
    rc |= shmemfile_testGoodAlloc(2000);
    rc |= shmemfile_testGoodAlloc(100000);

    /* util */
    rc |= util_testLog2();
    rc |= util_testPow2k();

    return rc;
}
