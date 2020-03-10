#include "shd-buddy.h"
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

static int buddy_test() {

    int rc = 0;

    size_t pool_nbytes = 128;
    size_t meta_nbytes = buddy_metaSizeNBytes(pool_nbytes);

    void *pool = calloc(1, pool_nbytes);
    void *meta = calloc(1, meta_nbytes);

    buddy_poolInit(pool, pool_nbytes);
    buddy_metaInit(meta, pool, pool_nbytes);

    buddy_alloc(32, meta, pool, pool_nbytes);
    buddy_alloc(16, meta, pool, pool_nbytes);
    buddy_alloc(16, meta, pool, pool_nbytes);
    buddy_alloc(16, meta, pool, pool_nbytes);

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

int main(int argc, char** argv) {

    int rc = 0;

    /* buddy */
    rc |= buddycontrolblock_testOrder();
    rc |= buddycontrolblock_testOrderAndNxt();
    rc |= buddycontrolblock_testTagAndPrv();
    rc |= buddycontrolblock_testGoodSizes();

    rc |= buddy_test();

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
