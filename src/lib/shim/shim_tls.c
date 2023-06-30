#include "lib/shim/shim_tls.h"

ShimThreadLocalStorage* shim_native_tls() {
    static __thread ShimThreadLocalStorage _tls = {0};
    return &_tls;
}