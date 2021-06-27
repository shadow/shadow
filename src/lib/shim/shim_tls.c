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

int shimtls_getCurrentIdx();
int shimtls_takeNextIdx();

typedef struct {
    bool active;
    const void* parentStackTopBound;
    int parentTlsIdx;
    const void* childStackTopBound;
    int childTlsIdx;
    bool recursedInChild;
} ShimTlsCloneState;
static ShimTlsCloneState _cloneState;

void shimtls_prepareClone(const void *childStackTopBound, const void *parentStackTopBound) {
    assert(!_cloneState.active);
    _cloneState = (ShimTlsCloneState) {
        .active = true,
        .parentStackTopBound = parentStackTopBound,
        .parentTlsIdx = shimtls_getCurrentIdx(),
        .childStackTopBound = childStackTopBound,
        .childTlsIdx = shimtls_takeNextIdx(),
        .recursedInChild = false,
    };
}
void shimtls_cloneDone() {
    assert(_cloneState.active);
    _cloneState.active = false;
}

// Each ShimTlsVar is assigned an offset in the ShimThreadLocalStorage's.
// This is the next free offset.
static size_t _nextByteOffset = 0;

// Index into _tlss.
static int _tlsIdx = 0;

// Take an unused TLS index, which can be used for a new thread.
int shimtls_takeNextIdx() {
    static int next = 0;
    assert(next < MAX_THREADS);
    return next++;
}

// Use when switching threads.
int shimtls_getCurrentIdx() {
    static __thread bool idxInitd = false;
    static __thread int idx;
    if (_cloneState.active) {
        if (_cloneState.recursedInChild) {
            void* rsp;
            GET_CURRENT_RSP(rsp);
            if (rsp > _cloneState.parentStackTopBound) {
                // We're past the parent bound, so must be in the child stack.
                return _cloneState.childTlsIdx;
            } else if (rsp > _cloneState.childStackTopBound) {
                // We're past the child top bound, so must be in the parent stack.
                return _cloneState.parentTlsIdx;
            } else if (_cloneState.parentStackTopBound < _cloneState.childStackTopBound) {
                return _cloneState.parentTlsIdx;
            } else {
                return _cloneState.childTlsIdx;
            }
        }
        _cloneState.recursedInChild = true;
        if (!idxInitd) {
            idx = _cloneState.childTlsIdx;
            idxInitd = true;
        }
        _cloneState.recursedInChild = false;
    }
    if (!idxInitd) {
        idx = shimtls_takeNextIdx();
        idxInitd = true;
    }
    return idx;
}

// Initialize storage and return whether it had already been initialized.
void* shimtlsvar_ptr(ShimTlsVar* v, size_t sz) {
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
    return &_tlss[shimtls_getCurrentIdx()]._bytes[v->_offset];
}
