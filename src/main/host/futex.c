/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/futex.h"

#include <errno.h>
#include <glib.h>
#include <stdatomic.h>

#include "main/core/support/definitions.h"
#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/utility/utility.h"
#include "support/logger/logger.h"

struct _Futex {
    // The unique address that is used to refer to this futex
    uint32_t* word;
    // Listeners waiting for wakups on this futex
    GHashTable* listeners;
    // Manage references
    int referenceCount;
    MAGIC_DECLARE;
};

Futex* futex_new(uint32_t* word) {
    Futex* futex = malloc(sizeof(*futex));
    *futex = (Futex){.word = word,
                     .listeners = g_hash_table_new_full(
                         g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)statuslistener_unref),
                     .referenceCount = 1,
                     MAGIC_INITIALIZER};

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
    if (--futex->referenceCount == 0) {
        _futex_free(futex);
    }
}

void futex_unref_func(void* futex) { futex_unref((Futex*)futex); }

uint32_t* futex_getAddress(Futex* futex) {
    MAGIC_ASSERT(futex);
    return futex->word;
}

unsigned int futex_wake(Futex* futex, unsigned int numWakeups) {
    MAGIC_ASSERT(futex);

    // We cannot use an iterator here, in case the hash table is modified
    // in the status changed callback.
    GList* listenerList = g_hash_table_get_keys(futex->listeners);

    // Iterate the listeners.
    GList* item = g_list_first(listenerList);
    unsigned int numWoken;
    for (numWoken = 0; item && numWoken < numWakeups; numWoken++) {
        StatusListener* listener = item->data;

        // Only call if the listener is still valid
        if (g_hash_table_contains(futex->listeners, listener)) {
            statuslistener_onStatusChanged(listener, STATUS_FUTEX_WAKEUP, STATUS_FUTEX_WAKEUP);
        }

        item = g_list_next(item);
    }

    g_list_free(listenerList);
    return numWoken;
}

void futex_addListener(Futex* futex, StatusListener* listener) {
    MAGIC_ASSERT(futex);
    utility_assert(listener);
    statuslistener_ref(listener);
    g_hash_table_insert(futex->listeners, listener, listener);
}

void futex_removeListener(Futex* futex, StatusListener* listener) {
    MAGIC_ASSERT(futex);
    g_hash_table_remove(futex->listeners, listener); // Will unref the listener
}

unsigned int futex_getListenerCount(Futex* futex) {
    MAGIC_ASSERT(futex);
    return g_hash_table_size(futex->listeners);
}
