/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

/* Implements an LD_PRELOAD library intended for use with Shadow. libcrypto
 * otherwise internally uses some entropy sources that Shadow is unable to trap
 * and emulate (such as the RDRAND instruction), making Shadow simulations of
 * software using libcrypto non-deterministic.
 *
 * To use this library, set LD_PRELOAD in the target program's `environment`.
 * (In Shadow, this is done for you with `--use-preload-openssl-rng true`.)
 */

#include <stddef.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "lib/shim/shim_api.h"

static int _getRandomBytes(unsigned char* buf, int numBytes) {
    // shadow interposes this and will fill the buffer for us
    // return 1 on success, 0 otherwise
    return (numBytes == shim_api_syscall(SYS_getrandom, buf, (size_t)numBytes, 0)) ? 1 : 0;
}

int RAND_DRBG_generate(void *drbg,
                        unsigned char *out, size_t outlen,
                        int prediction_resistance,
                        const unsigned char *adin, size_t adinlen) {
    return _getRandomBytes(out, outlen);
}

int RAND_DRBG_bytes(void *drbg,
                     unsigned char *out, size_t outlen) {
    return _getRandomBytes(out, outlen);
}

int RAND_bytes(unsigned char *buf, int num) {
    return _getRandomBytes(buf, num);
}

int RAND_pseudo_bytes(unsigned char *buf, int num) {
    return _getRandomBytes(buf, num);
}

void RAND_seed(const void *buf, int num) {
    return;
}

void RAND_add(const void *buf, int num, double entropy) {
    return;
}

int RAND_poll() {
    return 1;
}

void RAND_cleanup(void) {
    return;
}

int RAND_status(void) {
    return 1;
}

// Callback return type changed from void to int in OpenSSL_1_1_0-pre1.
// However, since x86-64 uses rax for return values, and rax is a caller-saved
// register, it's safe to return an int even if the caller is expecting void.
static int nop_seed(const void* buf, int num) { return 1; }

// Callback return type changed from void to int, and entropy from int to to
// double in OpenSSL_1_1_0-pre1.
//
// However, since x86-64 uses rax for return values, and rax is a caller-saved
// register, it's safe to return an int even if the caller is expecting void.
// Similarly, since we don't actually use either parameter, it doesn't matter if
// the types match.
static int nop_add(const void* buf, int num, double entropy) { return 1; }

typedef struct {
    int (*seed)(const void* buf, int num);
    int (*bytes)(unsigned char* buf, int num);
    void (*cleanup)(void);
    int (*add)(const void* buf, int num, double entropy);
    int (*pseudorand)(unsigned char* buf, int num);
    int (*status)(void);
} RAND_METHOD;

const RAND_METHOD* RAND_get_rand_method() {
    static const RAND_METHOD method = {
        .seed = nop_seed,
        .bytes = RAND_bytes,
        .cleanup = RAND_cleanup,
        .add = nop_add,
        .pseudorand = RAND_pseudo_bytes,
        .status = RAND_status,
    };
    return &method;
}