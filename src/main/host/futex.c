/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/futex.h"

#include <errno.h>
#include <stdatomic.h>

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _Futex {
    // The unique address that is used to refer to this futex
    uint32_t* word;

    int referenceCount;
    MAGIC_DECLARE;
};

Futex* futex_new(uint32_t* word) {
    Futex* futex = malloc(sizeof(*futex));
    *futex = (Futex){
        .word = word,
        .referenceCount = 1,
        MAGIC_INITIALIZER
    };
    
    worker_countObject(OBJECT_TYPE_FUTEX, COUNTER_TYPE_NEW);

    return futex;
}

static void _futex_free(Futex* futex) {
    MAGIC_ASSERT(futex);
    MAGIC_CLEAR(futex);
    free(futex);
    worker_countObject(OBJECT_TYPE_FUTEX, COUNTER_TYPE_FREE);
}

void futex_ref(Futex* futex) {
    MAGIC_ASSERT(futex);
    futex->referenceCount++;
}

void futex_unref(Futex* futex) {
    MAGIC_ASSERT(futex);
    utility_assert(futex->referenceCount > 0);
    if(--futex->referenceCount == 0) {
        _futex_free(futex);
    }
}

void futex_unref_func(void* futex) {
    futex_unref((Futex*) futex);
}

uint32_t futex_getValue(Futex* futex) {
    MAGIC_ASSERT(futex);
    return (uint32_t) atomic_load(futex->word);
}

uint32_t* futex_getAddress(Futex* futex) {
    MAGIC_ASSERT(futex);
    return futex->word;
}

void futex_wait(Futex* futex, Thread* thread, const struct timespec* timeout) {
    MAGIC_ASSERT(futex);

}

unsigned int futex_wake(Futex* futex, unsigned int numWakeups) {
    MAGIC_ASSERT(futex);
    return 0;
}

FutexState futex_checkState(Futex* futex, Thread* thread) {
    MAGIC_ASSERT(futex);
    return FUTEX_STATE_NONE;
}

