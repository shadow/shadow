#include "lib/shim/shim_tls.h"

#include "lib/shim/shim.h"

#include <assert.h>
#include <stdalign.h>

#define BYTES_PER_THREAD 1024
#define MAX_THREADS 100

// Stores the TLS for a single thread.
typedef struct ShimThreadLocalStorage {
    alignas(16) char _bytes[BYTES_PER_THREAD];
} ShimThreadLocalStorage;

// All TLSs. We could probably make this more dynamic if we need to.
static ShimThreadLocalStorage _tlss[MAX_THREADS];

// Each ShimTlsVar is assigned an offset in the ShimThreadLocalStorage's.
// This is the next free offset.
static size_t _nextByteOffset = 0;

// Index into _tlss.
static int _tlsIdx = 0;

static bool _useNativeTls;

static bool _initd = false;
void shimtls_init(bool useNativeTls) {
    assert(!_initd);
    _initd = true;
    _useNativeTls = useNativeTls;
}

static int _currentTlsIdx() {
    assert(_initd);
    if (!_useNativeTls) {
        // Unsafe to use thread-locals. idx will be set explicitly through shim IPC.
        return _tlsIdx;
    }

    // Thread locals supported.
    static int next_idx = 0;
    static __thread int idx = -1;
    if (idx == -1) {
        idx = next_idx++;
    }
    return idx;
}

// Initialize storage and return whether it had already been initialized.
void* shimtlsvar_ptr(ShimTlsVar* v, size_t sz) {
    assert(_initd);
    if (!v->_initd) {
        v->_offset = _nextByteOffset;
        _nextByteOffset += sz;

        // Always leave aligned at 16 for simplicity.
        // 16 is a safe alignment for any C primitive.
        size_t overhang = _nextByteOffset % 16;
        _nextByteOffset += (16 - overhang);

        assert(_nextByteOffset < sizeof(ShimThreadLocalStorage));
        v->_initd = true;
    }
    return &_tlss[_currentTlsIdx()]._bytes[v->_offset];
}

// Take an unused TLS index, which can be used for a new thread.
int shimtls_takeNextIdx() {
    assert(_initd);
    if (_useNativeTls) {
        return 0;
    }
    static int next = 0;
    assert(next < MAX_THREADS);
    return next++;
}

// Use when switching threads.
int shimtls_getCurrentIdx() {
    assert(_initd);
    if (_useNativeTls) {
        return 0;
    }
    return _tlsIdx;

}
void shimtls_setCurrentIdx(int idx) {
    assert(_initd);
    if (_useNativeTls) {
        return;
    }
    _tlsIdx = idx;
}