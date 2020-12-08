/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "main/host/descriptor/descriptor_table.h"

#include <glib.h>
#include <stdbool.h>
#include <unistd.h>

#include "main/core/support/object_counter.h"
#include "main/core/worker.h"
#include "main/host/descriptor/channel.h"
#include "main/host/descriptor/epoll.h"
#include "main/host/descriptor/tcp.h"
#include "main/utility/utility.h"

struct _DescriptorTable {
    /* All descriptors that we are tracking. Each descriptor has a unique
     * fd descriptor number associated with it when it is stored in our table,
     * which we refer to as a table index or table indices. */
    GHashTable* descriptors;

    /* Table indices that were previously allocated to store descriptors but
     * are no longer in use and are available to reallocate. The indices are
     * sorted lowest to highest so that the head of a non-empty queue is always
     * the index we should allocate. If the queue is empty, then we instead
     * use and increment the index counter. */
    GQueue* availableIndices;

    /* The highest currently allocated index we use to store any descriptor. */
    int indexCounter;

    /* Memory accounting. */
    int referenceCount;
    MAGIC_DECLARE;
};

DescriptorTable* descriptortable_new() {
    DescriptorTable* table = malloc(sizeof(DescriptorTable));

    *table = (DescriptorTable){
        .descriptors = g_hash_table_new_full(
            g_direct_hash, g_direct_equal, NULL, descriptor_unref),
        .availableIndices = g_queue_new(),
        .indexCounter = STDERR_FILENO, // the first allocatable index is 3
        .referenceCount = 1,
        MAGIC_INITIALIZER};

    worker_countObject(OBJECT_TYPE_DESCRIPTOR_TABLE, COUNTER_TYPE_NEW);
    return table;
}

static void _descriptortable_free(DescriptorTable* table) {
    MAGIC_ASSERT(table);

    if (table->descriptors) {
        g_hash_table_destroy(table->descriptors);
    }

    if (table->availableIndices) {
        g_queue_free(table->availableIndices);
    }

    MAGIC_CLEAR(table);
    free(table);
    worker_countObject(OBJECT_TYPE_DESCRIPTOR_TABLE, COUNTER_TYPE_FREE);
}

void descriptortable_ref(DescriptorTable* table) {
    MAGIC_ASSERT(table);
    table->referenceCount++;
}

void descriptortable_unref(DescriptorTable* table) {
    MAGIC_ASSERT(table);
    table->referenceCount--;
    utility_assert(table->referenceCount >= 0);
    if (table->referenceCount == 0) {
        _descriptortable_free(table);
    }
}

int descriptortable_add(DescriptorTable* table, Descriptor* descriptor) {
    MAGIC_ASSERT(table);

    int index = 0;

    if (g_queue_get_length(table->availableIndices) > 0) {
        index = GPOINTER_TO_INT(g_queue_pop_head(table->availableIndices));
    } else {
        index = ++table->indexCounter;
    }

    utility_assert(
        !g_hash_table_contains(table->descriptors, GINT_TO_POINTER(index)));

    g_hash_table_insert(table->descriptors, GINT_TO_POINTER(index), descriptor);

    descriptor_setHandle(descriptor, index);
    return index;
}

/* Compare function for sorting the indices queue from low to high valued ints.
 * Returns -1 if a < b, 0 if a == b, and +1 if a > b. */
static gint _descriptortable_compareInts(gconstpointer a, gconstpointer b,
                                         gpointer data) {
    gint aint = GPOINTER_TO_INT(a);
    gint bint = GPOINTER_TO_INT(b);
    return (a > b) - (a < b);
}

/* Remove unnecessary items from the tail of the indices queue. */
static void _descriptortable_trimIndicesTail(DescriptorTable* table) {
    MAGIC_ASSERT(table);
    while (GPOINTER_TO_INT(g_queue_peek_tail(table->availableIndices)) ==
           table->indexCounter) {
        g_queue_pop_tail(table->availableIndices);
        table->indexCounter--;
    }
}

bool descriptortable_remove(DescriptorTable* table, Descriptor* descriptor) {
    MAGIC_ASSERT(table);

    int index = descriptor_getHandle(descriptor);

    if (g_hash_table_contains(table->descriptors, GINT_TO_POINTER(index))) {
        /* Make sure we do not operate on the descriptor after we remove it,
         * because that could cause it to be freed and invalidate it. */
        descriptor_setHandle(descriptor, 0);
        g_hash_table_remove(table->descriptors, GINT_TO_POINTER(index));
        g_queue_insert_sorted(table->availableIndices, GINT_TO_POINTER(index),
                              _descriptortable_compareInts, NULL);
        _descriptortable_trimIndicesTail(table);
        return true;
    } else {
        return false;
    }
}

Descriptor* descriptortable_get(DescriptorTable* table, int index) {
    MAGIC_ASSERT(table);
    return g_hash_table_lookup(table->descriptors, GINT_TO_POINTER(index));
}

void descriptortable_set(DescriptorTable* table, int index,
                         Descriptor* descriptor) {
    MAGIC_ASSERT(table);

    /* We may be replacing a descriptor that is already set at index. */
    Descriptor* existing = descriptortable_get(table, index);
    if (existing) {
        descriptor_setHandle(existing, 0);
    }

    /* Store the new descriptor at the index. */
    g_hash_table_insert(table->descriptors, GINT_TO_POINTER(index), descriptor);
    descriptor_setHandle(descriptor, index);
}

/* TODO: remove this once the TCP layer is better designed. */
void descriptortable_shutdownHelper(DescriptorTable* table) {
    MAGIC_ASSERT(table);

    if (!table->descriptors) {
        return;
    }

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, table->descriptors);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        Descriptor* desc = value;
        if (desc && desc->type == DT_TCPSOCKET) {
            /* tcp servers and their children holds refs to each other. make
             * sure they all get freed by removing the refs in one direction */
            tcp_clearAllChildrenIfServer((TCP*)desc);
        } else if (desc &&
                   (desc->type == DT_UNIXSOCKET || desc->type == DT_PIPE)) {
            /* we need to correctly update the linked channel refs */
            channel_setLinkedChannel((Channel*)desc, NULL);
        } else if (desc && desc->type == DT_EPOLL) {
            epoll_clearWatchListeners((Epoll*)desc);
        }
    }
}
