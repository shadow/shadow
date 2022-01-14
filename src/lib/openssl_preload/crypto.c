/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <stdio.h>
#include <string.h>

#ifdef DEBUG
static unsigned long aes_e_cnt = 0;
static unsigned long aes_d_cnt = 0;
static unsigned long aes_ce_cnt = 0;
static unsigned long cry_ce_cnt = 0;
static unsigned long cry_cec_cnt = 0;
static unsigned long evp_cnt = 0;

static void _print_counters() {
    fprintf(stderr,
            "Counters: {'AES_encrypt':%lu, 'AES_decrypt':%lu, 'AES_ctr128_encrypt':%lu, "
            "'CRYPTO_ctr128_encrypt':%lu, 'CRYPTO_ctr128_encrypt_ctr32':%lu, 'EVP_Cipher':%lu}\n",
            aes_e_cnt, aes_d_cnt, aes_ce_cnt, cry_ce_cnt, cry_cec_cnt, evp_cnt);
}

__attribute__((constructor)) void _crypto_load() {
    fprintf(stderr, "Loading the preloaded crypto interception lib\n");
}

__attribute__((destructor)) void _crypto_unload() {
    fprintf(stderr, "Unloading the preloaded crypto interception lib\n");
    _print_counters();
}

#define _maybe_print_counters(cnt) { \
    if((cnt % 1000) == 0) {          \
        _print_counters();           \
    }                                \
}
#endif

void AES_encrypt(const unsigned char* in, unsigned char* out, const void* key) {
#ifdef DEBUG
    _maybe_print_counters(++aes_e_cnt);
#endif
}

void AES_decrypt(const unsigned char* in, unsigned char* out, const void* key) {
#ifdef DEBUG
    _maybe_print_counters(++aes_d_cnt);
#endif
}

void AES_ctr128_encrypt(const unsigned char* in, unsigned char* out, const void* key) {
#ifdef DEBUG
    _maybe_print_counters(++aes_ce_cnt);
#endif
}

void CRYPTO_ctr128_encrypt(const unsigned char *in, unsigned char *out, size_t len, ...) {
#ifdef DEBUG
    _maybe_print_counters(++cry_ce_cnt);
#endif
    memmove(out, in, len);
}

void CRYPTO_ctr128_encrypt_ctr32(const unsigned char *in, unsigned char *out, size_t len, ...) {
#ifdef DEBUG
    _maybe_print_counters(++cry_cec_cnt);
#endif
    memmove(out, in, len);
}

int EVP_Cipher(void* ctx, unsigned char* out, const unsigned char* in, unsigned int inl) {
#ifdef DEBUG
    _maybe_print_counters(++evp_cnt);
#endif
    memmove(out, in, (size_t)inl);
    return 1;
}
