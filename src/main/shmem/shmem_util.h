#ifndef SHD_SHMEM_UTIL_H_
#define SHD_SHMEM_UTIL_H_

#include <stdint.h>
#include <stdio.h>

/* Intended to be private to shd-shmem-allocator. */

static inline uint32_t shmem_util_uintPow2k(unsigned k) {
    return (1 << k);
}

static inline uint32_t shmem_util_roundUpPow2(uint32_t x) {
    int pc = __builtin_popcount(x);
    int shift = (pc != 1);
    return 1 << (shift + 32 - __builtin_clz(x) - 1);
}

uint32_t shmem_util_uintLog2(uint32_t v);

#endif // SHD_SHMEM_UTIL_H_
