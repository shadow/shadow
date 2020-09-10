/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/futex.h"

#include <errno.h>

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/descriptor.h"
#include "main/host/descriptor/descriptor_types.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _Futex {
    Descriptor super;
    // The address that is used to refer to this futex
    int* word;
    MAGIC_DECLARE;
};

static Futex* _futex_fromDescriptor(Descriptor* descriptor) {
    utility_assert(descriptor_getType(descriptor) == DT_FUTEX);
    return (Futex*)descriptor;
}

static gboolean _futex_close(Descriptor* descriptor) {
    Futex* futex = _futex_fromDescriptor(descriptor);
    MAGIC_ASSERT(futex);
    debug("futex word %p closing now", futex->word);
    descriptor_adjustStatus(&(futex->super), DS_ACTIVE, FALSE);
    if (futex->super.handle > 0) {
        return TRUE; // deregister from process
    } else {
        return FALSE; // we are not owned by a process
    }
}

static void _futex_free(Descriptor* descriptor) {
    Futex* futex = _futex_fromDescriptor(descriptor);
    MAGIC_ASSERT(futex);
    descriptor_clear((Descriptor*)futex);
    MAGIC_CLEAR(futex);
    g_free(futex);
    worker_countObject(OBJECT_TYPE_FUTEX, COUNTER_TYPE_FREE);
}

static DescriptorFunctionTable _futexFunctions = {
    _futex_close, _futex_free, MAGIC_VALUE};

Futex* futex_new() {
    Futex* futex = g_new0(Futex, 1);
    MAGIC_INIT(futex);

    descriptor_init(&(futex->super), DT_FUTEX, &_futexFunctions);
    descriptor_adjustStatus(&(futex->super), DS_ACTIVE, TRUE);

    worker_countObject(OBJECT_TYPE_FUTEX, COUNTER_TYPE_NEW);

    return futex;
}

