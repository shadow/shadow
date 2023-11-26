/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "main/utility/priority_queue.h"

#include <glib.h>
#include <stddef.h>

#include "main/utility/priority_queue.h"
#include "main/utility/utility.h"

static const gsize INITIAL_SIZE = 100;

struct _PriorityQueue {
    gpointer *heap;
    GHashTable *map;
    gsize size;
    gsize heapSize;
    GCompareDataFunc compareFunc;
    gpointer compareData;
    GDestroyNotify freeFunc;
};

PriorityQueue* priorityqueue_new(GCompareDataFunc compareFunc, gpointer compareData,
                                 GDestroyNotify freeFunc, GHashFunc hashFunc, GEqualFunc eqFunc) {
    utility_debugAssert(compareFunc);
    PriorityQueue *q = g_slice_new(PriorityQueue);
    q->heap = g_new(gpointer, INITIAL_SIZE);
    q->map = g_hash_table_new(hashFunc, eqFunc);
    q->size = 0;
    q->heapSize = INITIAL_SIZE;
    q->compareFunc = compareFunc;
    q->compareData = compareData;
    q->freeFunc = freeFunc;
    return q;
}

void priorityqueue_clear(PriorityQueue *q) {
    utility_debugAssert(q);
    if(q->freeFunc) {
        for (guint i = 0; i < q->size; i++) {
            q->freeFunc(q->heap[i]);
            q->heap[i] = NULL;
        }
    }
    q->size = 0;
    g_hash_table_remove_all(q->map);
}

void priorityqueue_free(PriorityQueue *q) {
    utility_debugAssert(q);
    priorityqueue_clear(q);
    g_hash_table_destroy(q->map);
    g_free(q->heap);
    g_slice_free(PriorityQueue, q);
}

gsize priorityqueue_getLength(PriorityQueue *q) {
    utility_debugAssert(q);
    return q->size;
}

gboolean priorityqueue_isEmpty(PriorityQueue *q) {
    utility_debugAssert(q);
    return q->size == 0;
}

static void _priorityqueue_refresh_map(PriorityQueue *q) {
    g_hash_table_remove_all(q->map);
    for (guint i = 0; i < q->size; i++) {
        g_hash_table_insert(q->map, q->heap[i], q->heap + i);
    }
}

static void _priorityqueue_swap_entries(PriorityQueue *q, guint i, guint j) {
    gpointer pi = q->heap[i];
    gpointer pj = q->heap[j];
    q->heap[i] = pj;
    q->heap[j] = pi;
    g_hash_table_insert(q->map, pi, q->heap + j);
    g_hash_table_insert(q->map, pj, q->heap + i);
}

static gboolean _priorityqueue_entry_smaller(PriorityQueue *q, guint i, guint j) {
    return q->compareFunc(q->heap[i], q->heap[j], q->compareData) < 0;
}

static guint _priorityqueue_heapify_up(PriorityQueue *q, guint index) {
    while ((index > 0) && _priorityqueue_entry_smaller(q, index, (index - 1) / 2)) {
        _priorityqueue_swap_entries(q, index, (index - 1) / 2);
        index = (index - 1) / 2;
    }
    return index;
}

static guint _priorityqueue_heapify_down(PriorityQueue *q, guint index) {
    guint child;
    while ((child = 2 * index + 1) < q->size) {
        if ((child + 1 < q->size) && _priorityqueue_entry_smaller(q, child + 1, child)) {
            child = child + 1;
        }
        if (_priorityqueue_entry_smaller(q, child, index)) {
            _priorityqueue_swap_entries(q, index, child);
            index = child;
        } else {
            break;
        }
    }
    return index;
}

gboolean priorityqueue_push(PriorityQueue *q, gpointer data) {
    utility_debugAssert(q);
    if (q->size >= q->heapSize) {
        q->heapSize *= 2;
        gpointer *oldheap = q->heap;
        q->heap = g_renew(gpointer, q->heap, q->heapSize);
        if (q->heap != oldheap) {
            _priorityqueue_refresh_map(q);
        }
    }

    gpointer *oldentry = g_hash_table_lookup(q->map, data);
    if (oldentry != NULL) {
        gint oldindex = oldentry - q->heap;
        _priorityqueue_heapify_up(q, _priorityqueue_heapify_down(q, oldindex));
        return FALSE;
    }

    guint index = q->size;
    q->heap[index] = data;
    g_hash_table_insert(q->map, data, q->heap + index);
    q->size += 1;
    _priorityqueue_heapify_up(q, index);

    return TRUE;
}

gpointer priorityqueue_peek(PriorityQueue *q) {
    utility_debugAssert(q);
    if (q->size > 0) {
        return q->heap[0];
    }
    return NULL;
}

gpointer priorityqueue_find(PriorityQueue *q, gpointer data) {
    utility_debugAssert(q);
    gpointer *entry = g_hash_table_lookup(q->map, data);
    return (entry == NULL) ? NULL : *entry;
}

gpointer priorityqueue_pop(PriorityQueue *q) {
    utility_debugAssert(q);
    if (q->size > 0) {
        gpointer data = q->heap[0];
        _priorityqueue_swap_entries(q, 0, q->size - 1);
        g_hash_table_remove(q->map, data);
        q->size -= 1;
        _priorityqueue_heapify_down(q, 0);
        if ((q->heapSize > INITIAL_SIZE) && (q->size * 4 < q->heapSize)) {
            q->heapSize /= 2;
            gpointer *oldheap = q->heap;
            q->heap = g_renew(gpointer, q->heap, q->heapSize);
            if (q->heap != oldheap) {
                _priorityqueue_refresh_map(q);
            }
        }
        return data;
    }
    return NULL;
}
