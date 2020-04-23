#ifndef SHD_SHMEM_UTIL_H_
#define SHD_SHMEM_UTIL_H_

#include <stdint.h>
#include <stdio.h>

/* Intended to be private to shd-shmem-allocator. */

/* rwails:
 *
 * Compile-time log selection enables the shmem modules to be built with no
 * dependency on shadow, which makes it easy to write unit tests.
 *
 * TODO: Clean this up -- perhaps replace with glib logging?
 */

#ifdef SHD_SHMEM_LOG_LOG_SHADOW
#include "shadow.h"
#define SHD_SHMEM_LOG_ERROR(...) (error(__VA_ARGS__))
#else
#define SHD_SHMEM_LOG_ERROR(...)                                               \
    fprintf(stderr, __VA_ARGS__);                                              \
    fputs("\n", stderr)
#endif // SHD_SHMEM_LOG_LOG_SHADOW

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
