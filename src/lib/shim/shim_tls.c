#include "lib/shim/shim_tls.h"

ShimThreadLocalStorageAllocation* shim_native_tls() {
    static __thread ShimThreadLocalStorageAllocation _tls = {0};
    return &_tls;
}