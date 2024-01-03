/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/futex_table.h"

#include <glib.h>
#include <stdbool.h>
#include <unistd.h>

#include "main/bindings/c/bindings-opaque.h"
#include "main/core/worker.h"
#include "main/host/futex.h"
#include "main/utility/utility.h"

struct _FutexTable {
    /* All futexes that we are tracking. Each futex has a unique physical address associated with it
     * when it is stored in our table, which we refer to as a table index or table indices. Maps
     * ManagedPhysicalMemoryAddr to Futex*. */
    GHashTable* futexes;

    /* Memory accounting. */
    int referenceCount;
    MAGIC_DECLARE;
};

FutexTable* futextable_new() {
    FutexTable* table = malloc(sizeof(FutexTable));

    *table = (FutexTable){.referenceCount = 1, MAGIC_INITIALIZER};

    worker_count_allocation(FutexTable);
    return table;
}

static void _futextable_free(FutexTable* table) {
    MAGIC_ASSERT(table);

    if (table->futexes) {
        g_hash_table_destroy(table->futexes);
    }

    MAGIC_CLEAR(table);
    free(table);
    worker_count_deallocation(FutexTable);
}

void futextable_ref(FutexTable* table) {
    MAGIC_ASSERT(table);
    table->referenceCount++;
}

void futextable_unref(FutexTable* table) {
    MAGIC_ASSERT(table);
    table->referenceCount--;
    utility_debugAssert(table->referenceCount >= 0);
    if (table->referenceCount == 0) {
        _futextable_free(table);
    }
}

bool futextable_add(FutexTable* table, Futex* futex) {
    MAGIC_ASSERT(table);

    ManagedPhysicalMemoryAddr ptr = futex_getAddress(futex);
    gpointer index = GUINT_TO_POINTER(ptr.val);

    if (!table->futexes) {
        table->futexes =
            g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, futex_unref_func);
    }

    if (g_hash_table_contains(table->futexes, index)) {
        return false;
    } else {
        g_hash_table_insert(table->futexes, index, futex);
        return true;
    }
}

bool futextable_remove(FutexTable* table, Futex* futex) {
    MAGIC_ASSERT(table);

    ManagedPhysicalMemoryAddr ptr = futex_getAddress(futex);
    gpointer index = GUINT_TO_POINTER(ptr.val);

    if (table->futexes && g_hash_table_contains(table->futexes, index)) {
        g_hash_table_remove(table->futexes, index);
        return true;
    } else {
        return false;
    }
}

Futex* futextable_get(FutexTable* table, ManagedPhysicalMemoryAddr ptr) {
    MAGIC_ASSERT(table);

    gpointer index = GUINT_TO_POINTER(ptr.val);

    if (table->futexes) {
        return g_hash_table_lookup(table->futexes, index);
    } else {
        return NULL;
    }
}
