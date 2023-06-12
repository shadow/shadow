#include "lib/shim/shim_tls.h"

#include "lib/logger/logger.h"
#include "lib/shim/shim.h"
#include "lib/shim/shim_seccomp.h"
#include "lib/shim/shim_syscall.h"
#include "main/utility/syscall.h"

#include <assert.h>
#include <stdalign.h>
#include <sys/mman.h>
#include <sys/syscall.h>

// This needs to be big enough to store all thread-local variables for a single
// thread. We fail at runtime if this limit is exceeded.
#define BYTES_PER_THREAD 1024

// Max threads for our slow TLS fallback mechanism.
#define TLS_FALLBACK_MAX_THREADS 1000

// Stores the TLS for a single thread.
typedef struct ShimThreadLocalStorage {
    alignas(16) char _bytes[BYTES_PER_THREAD];
} ShimThreadLocalStorage;

// Get the shim's TLS for the current thread. We avoid directly using native
// TLS throughout the shim, since this is a libc dependency.
static ShimThreadLocalStorage* _tls_storage() {
    // Linux's native TLS uses the %fs register when it's set up.
    // When it's not in use, %fs should be 0.
    void* fs = NULL;
    __asm__("mov %%fs:0x0, %0" : "=r"(fs)::);

    // Native TLS results in a recursion loop under asan
    if (false && fs != NULL) {
        // Native (libc) TLS seems to be set up properly. Use it.
        static __thread ShimThreadLocalStorage _tls = {0};
        return &_tls;
    }

    // Hacky, slow TLS to support bare calls to `clone`.
    // Mostly to let us test bare clone calls; real programs
    // will almost certainly use native TLS.

    // Parallel arrays mapping thread IDS to backing storage.
    static pid_t* thread_ids=NULL;
    static ShimThreadLocalStorage* global_tls=NULL;

    // Lazy allocation, since we normally don't need this.
    // Allocate all at once for simplicity.
    static bool initd = false;
    if (!initd) {
        thread_ids = (void*)shim_native_syscall(
            NULL, SYS_mmap, NULL, sizeof(pid_t) * TLS_FALLBACK_MAX_THREADS, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        global_tls = (void*)shim_native_syscall(
            NULL, SYS_mmap, NULL, sizeof(ShimThreadLocalStorage) * TLS_FALLBACK_MAX_THREADS,
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (syscall_rawReturnValueToErrno((long)thread_ids) != 0 ||
            syscall_rawReturnValueToErrno((long)global_tls != 0)) {
            // We failed to allocate. Crash.
            shim_native_syscall(NULL, SYS_kill, 0, SIGABRT);
            asm("ud2");
        }
        initd = true;
    }

    pid_t tid = (pid_t)shim_native_syscall(NULL, SYS_gettid);
    for (size_t i = 0; i < TLS_FALLBACK_MAX_THREADS; ++i) {
        if (thread_ids[i] == tid) {
            // Already one allocated for this thread.
            return &global_tls[i];
        }
        if (thread_ids[i] == 0) {
            // Use this slot.
            thread_ids[i] = tid;
            return &global_tls[i];
        }
    }

    // We ran out of slots. Either increase TLS_FALLBACK_MAX_THREADS, or extend
    // to support reuse after thread exit and/or dynamic growth.
    shim_native_syscall(NULL, SYS_kill, 0, SIGABRT);
    asm("ud2");

    return NULL;
}

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
    return &_tls_storage()->_bytes[v->_offset];
}
