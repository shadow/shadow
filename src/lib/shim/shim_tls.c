#include "lib/shim/shim_tls.h"

TlsOneThreadStorageAllocation* shim_native_tls() {
    static __thread TlsOneThreadStorageAllocation _tls = {0};
    return &_tls;
}