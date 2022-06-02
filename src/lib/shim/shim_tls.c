#include "lib/shim/shim_tls.h"

#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_syscall.h"

#include <assert.h>
#include <stdalign.h>
#include <sys/mman.h>
#include <sys/syscall.h>

// This needs to be big enough to store all thread-local variables for a single
// thread. We fail at runtime if this limit is exceeded.
#define BYTES_PER_THREAD 1024

// Stores the TLS for a single thread.
typedef struct ShimThreadLocalStorage {
    alignas(16) char _bytes[BYTES_PER_THREAD];
} ShimThreadLocalStorage;

// The shim's TLS for the current thread. We currently only store a pointer in
// native TLS, which is dynamically allocated when the thread starts, and leaks
// when the thread exits.
//
// Ideally we would allocate the ShimThreadLocalStorage itself in native TLS,
// which would remove the leak, but changing the memory protections to set up
// the stack guard page in _shim_init_signal_stack breaks glibc's TLS allocator.
// If we want to do this in the future maybe we can try to revert the
// protections just before the thread exits.
//
// We could alternatively change `_shim_init_signal_stack` to dynamically
// allocate its stack instead of using TLS, but that'd just move the leak there.
static __thread ShimThreadLocalStorage _tls = {0};

// Each ShimTlsVar is assigned an offset in the ShimThreadLocalStorage's.
// This is the next free offset.
static size_t _nextByteOffset = 0;

// Index into _tlss.
static int _tlsIdx = 0;

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
    return &_tls._bytes[v->_offset];
}
