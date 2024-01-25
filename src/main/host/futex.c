/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/futex.h"

#include <errno.h>
#include <glib.h>
#include <stdbool.h>

#include "lib/logger/logger.h"
#include "main/bindings/c/bindings-opaque.h"
#include "main/core/definitions.h"
#include "main/core/worker.h"
#include "main/utility/utility.h"

struct _Futex {
    // The unique physical address that is used to refer to this futex
    ManagedPhysicalMemoryAddr word;
    // Listeners waiting for wakups on this futex
    // The key is a listener of type StatusListener*, the value is a boolean that indicates
    // whether or not a wakeup has already been performed on the listener.
    GHashTable* listeners;
    // Manage references
    int referenceCount;
    MAGIC_DECLARE;
};

Futex* futex_new(ManagedPhysicalMemoryAddr word) {
    Futex* futex = malloc(sizeof(*futex));
    *futex = (Futex){.word = word,
                     .listeners = g_hash_table_new_full(
                         g_direct_hash, g_direct_equal, (GDestroyNotify)statuslistener_unref, NULL),
                     .referenceCount = 1,
                     MAGIC_INITIALIZER};

    worker_count_allocation(Futex);

    return futex;
}

static void _futex_free(Futex* futex) {
    MAGIC_ASSERT(futex);

    g_hash_table_destroy(futex->listeners);

    MAGIC_CLEAR(futex);
    free(futex);
    worker_count_deallocation(Futex);
}

void futex_ref(Futex* futex) {
    MAGIC_ASSERT(futex);
    futex->referenceCount++;
}

void futex_unref(Futex* futex) {
    MAGIC_ASSERT(futex);
    utility_debugAssert(futex->referenceCount > 0);
    if (--futex->referenceCount == 0) {
        _futex_free(futex);
    }
}

void futex_unref_func(void* futex) { futex_unref((Futex*)futex); }

ManagedPhysicalMemoryAddr futex_getAddress(Futex* futex) {
    MAGIC_ASSERT(futex);
    return futex->word;
}

unsigned int futex_wake(Futex* futex, unsigned int numWakeups) {
    MAGIC_ASSERT(futex);

    // We cannot use an iterator here, in case the hash table is modified
    // in the status changed callback.
    GList* listenerList = g_hash_table_get_keys(futex->listeners);

    // Iterate the listeners in deterministic order and perform the requested
    // number of wakeups if we can. It's probably better to maintain the items
    // in a sorted structure, e.g. a ring, to make it easier / more efficient to
    // iterate deterministically while also moving the ring entry pointer so we
    // don't always wake up the same listener first on every iteration and
    // possibly starve the others.
    GList* item = NULL;
    if(listenerList != NULL) {
        listenerList = g_list_sort(listenerList, status_listener_compare);
        item = g_list_first(listenerList);
    }

    unsigned int numWoken = 0;

    while (item && (numWoken < numWakeups)) {
        StatusListener* listener = item->data;

        // Only call if the listener is still valid
        if (g_hash_table_contains(futex->listeners, listener)) {
            // If this listener was already woken up, skip it this time
            bool did_wakeup =
                (bool)GPOINTER_TO_UINT(g_hash_table_lookup(futex->listeners, listener));
            if (!did_wakeup) {
                // Tell the status listener to unblock the thread waiting on the futex
                statuslistener_onStatusChanged(listener, FileState_FUTEX_WAKEUP, FileState_FUTEX_WAKEUP);

                // Track that we did a wakeup on this listener without destroying the listener
                g_hash_table_steal(futex->listeners, listener);
                g_hash_table_insert(futex->listeners, listener, GUINT_TO_POINTER(true));

                // Count the wake-up
                numWoken++;
            }
        }

        item = g_list_next(item);
    }

    if(listenerList != NULL) {
        g_list_free(listenerList);
    }
    return numWoken;
}

void futex_addListener(Futex* futex, StatusListener* listener) {
    MAGIC_ASSERT(futex);
    utility_debugAssert(listener);
    statuslistener_ref(listener);
    g_hash_table_insert(futex->listeners, listener, GUINT_TO_POINTER(false));
}

void futex_removeListener(Futex* futex, StatusListener* listener) {
    MAGIC_ASSERT(futex);
    g_hash_table_remove(futex->listeners, listener); // Will unref the listener
}

unsigned int futex_getListenerCount(Futex* futex) {
    MAGIC_ASSERT(futex);
    return g_hash_table_size(futex->listeners);
}
