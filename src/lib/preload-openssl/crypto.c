/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEBUG
#define debuglog(...) fprintf(stderr, __VA_ARGS__)
#else
#define debuglog(...)
#endif

// Caches whether or not a memory address is from libssl.so
typedef struct {
    void* addr;
    bool is_libssl;
} bt_cache_entry_t;

// Lock for safely accessing this lib's global state from multiple threads.
// TODO: We currently do no error checking when operating on this lock.
static pthread_mutex_t global_state_lock = PTHREAD_MUTEX_INITIALIZER;

// We use a small cache size: in ad-hoc experiments with tor-0.4.6.9, we observed
// at most three callers of EVP_EncryptUpdate.
#define EVP_BACKTRACE_CACHE_LEN 10
static bt_cache_entry_t evp_backtrace_cache[EVP_BACKTRACE_CACHE_LEN];

// For storing a pointer to the original EVP_EncryptUpdate function.
typedef int EVP_EncryptUpdate_func(void*, unsigned char*, int*, const unsigned char*, int);
static void* evp_eu_funcptr = NULL;

// Counters for verifying that interception is happening correctly.
static unsigned long aes_e_cnt = 0;
static unsigned long aes_d_cnt = 0;
static unsigned long aes_ce_cnt = 0;
static unsigned long crypto_ce_cnt = 0;
static unsigned long crypto_cec_cnt = 0;
static unsigned long evp_c_cnt = 0;
static unsigned long evp_eu_cnt = 0;

static void _print_counters() {
    debuglog("Counters: {'AES_encrypt':%lu, 'AES_decrypt':%lu, 'AES_ctr128_encrypt':%lu, "
             "'CRYPTO_ctr128_encrypt':%lu, 'CRYPTO_ctr128_encrypt_ctr32':%lu, "
             "'EVP_Cipher':%lu, 'EVP_EncryptUpdate':%lu}\n",
             aes_e_cnt, aes_d_cnt, aes_ce_cnt, crypto_ce_cnt, crypto_cec_cnt, evp_c_cnt,
             evp_eu_cnt);
}

static void _lock_global_state() {
    int result = pthread_mutex_lock(&global_state_lock);
    if (result != 0) {
        abort();
    }
}

static void _unlock_global_state() {
    int result = pthread_mutex_unlock(&global_state_lock);
    if (result != 0) {
        abort();
    }
}

__attribute__((constructor)) void _crypto_load() {
    debuglog("Loading the preloaded crypto interception lib\n");

    _lock_global_state();

    // Initialize backtrace address cache pointers to NULL.
    memset(evp_backtrace_cache, 0, sizeof(bt_cache_entry_t) * EVP_BACKTRACE_CACHE_LEN);

    // Get a ref to the EVP_EncryptUpdate that would be called if we didn't preload.
    evp_eu_funcptr = dlsym(RTLD_NEXT, "EVP_EncryptUpdate");

    debuglog("dlsym for EVP_EncryptUpdate returned %p\n", evp_eu_funcptr);

    _unlock_global_state();
}

__attribute__((destructor)) void _crypto_unload() {
    debuglog("Unloading the preloaded crypto interception lib\n");

    _lock_global_state();
    _print_counters();
    _unlock_global_state();
}

static void _increment_unlocked(unsigned long* cnt_ptr) {
    if ((++(*cnt_ptr) % 1000) == 0) {
        _print_counters();
    }
}

static void _increment(unsigned long* cnt_ptr) {
    _lock_global_state();
    _increment_unlocked(cnt_ptr);
    _unlock_global_state();
}

void AES_encrypt(const unsigned char* in, unsigned char* out, const void* key) {
    _increment(&aes_e_cnt);
}

void AES_decrypt(const unsigned char* in, unsigned char* out, const void* key) {
    _increment(&aes_d_cnt);
}

void AES_ctr128_encrypt(const unsigned char* in, unsigned char* out, const void* key) {
    _increment(&aes_ce_cnt);
}

void CRYPTO_ctr128_encrypt(const unsigned char* in, unsigned char* out, size_t len, ...) {
    _increment(&crypto_ce_cnt);
    memmove(out, in, len);
}

void CRYPTO_ctr128_encrypt_ctr32(const unsigned char* in, unsigned char* out, size_t len, ...) {
    _increment(&crypto_cec_cnt);
    memmove(out, in, len);
}

int EVP_Cipher(void* ctx, unsigned char* out, const unsigned char* in, unsigned int inl) {
    _increment(&evp_c_cnt);
    memmove(out, in, (size_t)inl);
    return 1;
}

static const bt_cache_entry_t* _get_cache_entry(void* addr) {
    // An in-order traversal is fine since the cache is small.
    // We can skip out early when we get to the first NULL pointer.
    for (int i = 0; i < EVP_BACKTRACE_CACHE_LEN && evp_backtrace_cache[i].addr != NULL; i++) {
        if (addr == evp_backtrace_cache[i].addr) {
            return &evp_backtrace_cache[i];
        }
    }
    return NULL;
}

static bool _cache_has_space() {
    return (evp_backtrace_cache[EVP_BACKTRACE_CACHE_LEN - 1].addr == NULL);
}

static void _append_to_cache(void* addr, bool is_libssl) {
    // Store at the first empty cache slot.
    for (int i = 0; i < EVP_BACKTRACE_CACHE_LEN; i++) {
        if (evp_backtrace_cache[i].addr == NULL) {
            evp_backtrace_cache[i].addr = addr;
            evp_backtrace_cache[i].is_libssl = is_libssl;

            debuglog("Cached EVP_EncryptUpdate caller=%p, is_libssl=%s\n", addr,
                     is_libssl ? "true" : "false");
            break;
        }
    }
}

static bool _is_addr_in_libssl(void* addr) {
    bool found = false;

    // Gets a string containing the full file path to the caller,
    // e.g. /lib/x86_64-linux-gnu/libssl.so.1.1
    char** bt_str_buf = backtrace_symbols(&addr, 1);

    if (bt_str_buf != NULL && bt_str_buf[0] != NULL) {
        found = (strstr(bt_str_buf[0], "libssl.so") != NULL);
    }

    free(bt_str_buf);
    return found;
}

int EVP_EncryptUpdate(void* cipher, unsigned char* out, int* outl, const unsigned char* in,
                      int inl) {
    // In the case of tor:
    //   - calls from libssl are used for TLS and skipping will break TLS
    //   - calls from tor are used for AES and can be skipped
    // So we can skip the crypto op as long as the call is not made from libssl.
    void* caller_addr = NULL;
    bool caller_is_libssl = false;

    {
        // Get the backtrace addresses; we only need 2: our stack is in [0]
        // and the caller's stack is in [1].
        void* bt_addr_buf[2];

        // We want to make sure we get the backtrace here and not in a helper
        // function so that our stack offset math is correct.
        int bt_len = backtrace(bt_addr_buf, 2);
        if (bt_len == 2) {
            caller_addr = bt_addr_buf[1];
        }
    }

    if (caller_addr != NULL) {
        _lock_global_state();

        // We use a cache first because checking the name of the library is expensive.
        const bt_cache_entry_t* entry = _get_cache_entry(caller_addr);

        if (entry != NULL) {
            // The answer we want is cached.
            caller_is_libssl = entry->is_libssl;
        } else {
            // Fall back to check the caller backtrace symbols.
            // This might be more expensive than just performing the crypto op, and we might have to
            // perform the crypto op anyway depending on the result, but we do it anyway to maintain
            // consistency in behavior.
            caller_is_libssl = _is_addr_in_libssl(caller_addr);
            // Cache it if there is space.
            if (_cache_has_space()) {
                _append_to_cache(caller_addr, caller_is_libssl);
            }
        }

        if (!caller_is_libssl) {
            // We will skip the crypto, increment the counter while we still hold the lock.
            _increment_unlocked(&evp_eu_cnt);
        }

        _unlock_global_state();
    }

    if (caller_addr != NULL && !caller_is_libssl) {
        // Skip the crypto in calls made from the application, e.g. tor.
        // We already incremented the counter above.
        return 1; // success
    } else if (evp_eu_funcptr != NULL) {
        // Let openssl handle it.
        return ((EVP_EncryptUpdate_func*)evp_eu_funcptr)(cipher, out, outl, in, inl);
    } else {
        // We counldn't find openssl's EVP_EncryptUpdate function pointer.
        return 0; // failure
    }
}
