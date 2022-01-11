#include "lib/shim/shim_tls.h"

#include "lib/logger/logger.h"
#include "lib/shim/shim.h"

#include <assert.h>
#include <stdalign.h>

// This needs to be big enough to store all thread-local variables for a single
// thread. We fail at runtime if this limit is exceeded.
//
// Right now the biggest contributors are special thread-local stacks in
// _shim_emulated_syscallv and in _shim_init_signal_stack. Each of those is
// 4096*10 bytes.
//
// Fixing https://github.com/shadow/shadow/issues/1846 will likely remove one
// of those, in which case we can reduce this allocation.
#define BYTES_PER_THREAD (2*4096*10 + 1024)
#define MAX_THREADS 100

// Stores the TLS for a single thread.
typedef struct ShimThreadLocalStorage {
    alignas(16) char _bytes[BYTES_PER_THREAD];
} ShimThreadLocalStorage;

// All TLSs. We could probably make this more dynamic if we need to.
//
// If we ever decide we want to more permanently just depend on native thread
// local storage, we could make this a single `__thread ShimThreadLocalStorage`
// and get rid of or ignore the indexes.
static ShimThreadLocalStorage _tlss[MAX_THREADS] = {0};

int shimtls_getCurrentIdx();
int shimtls_takeNextIdx();

// Each ShimTlsVar is assigned an offset in the ShimThreadLocalStorage's.
// This is the next free offset.
static size_t _nextByteOffset = 0;

// Index into _tlss.
static int _tlsIdx = 0;

// Take an unused TLS index, which can be used for a new thread.
int shimtls_takeNextIdx() {
    static int next = 0;
    if (next >= MAX_THREADS) {
        panic("Exceeded hard-coded limit of %d threads", MAX_THREADS);
    }
    return next++;
}

// Use when switching threads.
int shimtls_getCurrentIdx() {
    // For now this is the one place we use native thread local storage.  If we
    // do need to avoid depending on it, one possibility is to register the top
    // of each thread's stack with this module, and then here check the current
    // %RSP to determine which stack we're executing on. Leaving out that
    // complexity (and log(n) lookup) until if and when we need it.
    static __thread bool idxInitd = false;
    static __thread int idx;
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
        if (_nextByteOffset >= sizeof(ShimThreadLocalStorage)) {
            panic("Exceed hard-coded limit of %zu bytes of thread local storage",
                  sizeof(ShimThreadLocalStorage));
        }
        v->_initd = true;
    }
    return &_tlss[shimtls_getCurrentIdx()]._bytes[v->_offset];
}
